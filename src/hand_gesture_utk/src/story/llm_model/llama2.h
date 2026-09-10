/* Forked from llama2.c/runq.c (https://github.com/karpathy/llama2.c) */

/* Inference for Llama-2 Transformer model in pure C, int8 quantized forward pass. */

#include <math.h>
#include <vector>
#include <cstdint>
#include <cstring>

/* Helium (Arm M-Profile Vector Extension) accelerates the int8 matmul below.
 * The Cortex-M85 on the RA8P1 has MVE. If the build does not enable it, the code
 * falls back to the scalar loop (correct, just slower) and this warning fires. */
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 1)
#include <arm_mve.h>
#define LLAMA2_MVE 1
#else
#define LLAMA2_MVE 0
#warning "Helium/MVE not enabled: matmul uses the scalar path. Enable MVE (e.g. -mcpu=cortex-m85+mve) for the speedup."
#endif

// Similar API to fread() for reading from a byte array instead.
inline void sread(void *buffer, size_t size, uint8_t* *stream) {
    memcpy(buffer, *stream, size);
    *stream += size;
}

// ----------------------------------------------------------------------------
// Transformer model

typedef struct {
    int dim; // transformer dimension
    int hidden_dim; // for ffn layers
    int n_layers; // number of layers
    int n_heads; // number of query heads
    int n_kv_heads; // number of key/value heads (can be < query heads because of multiquery)
    int vocab_size; // vocabulary size, usually 256 (byte-level)
    int seq_len; // max sequence length
} Config;

typedef struct {
    int8_t* q;    // quantized values
    float* s; // scaling factors
} QuantizedTensor;

typedef struct {
    // token embedding table
    QuantizedTensor *q_tokens; // (vocab_size, dim)
    float* token_embedding_table; // same, but dequantized

    // weights for rmsnorms
    float* rms_att_weight; // (layer, dim) rmsnorm weights
    float* rms_ffn_weight; // (layer, dim)
    // weights for matmuls. note dim == n_heads * head_size
    QuantizedTensor *wq; // (layer, dim, n_heads * head_size)
    QuantizedTensor *wk; // (layer, dim, n_kv_heads * head_size)
    QuantizedTensor *wv; // (layer, dim, n_kv_heads * head_size)
    QuantizedTensor *wo; // (layer, n_heads * head_size, dim)
    // weights for ffn
    QuantizedTensor *w1; // (layer, hidden_dim, dim)
    QuantizedTensor *w2; // (layer, dim, hidden_dim)
    QuantizedTensor *w3; // (layer, hidden_dim, dim)
    // final rmsnorm
    float* rms_final_weight; // (dim,)
    // (optional) classifier weights for the logits, on the last layer
    QuantizedTensor *wcls;
} TransformerWeights;

typedef struct {
    // current wave of activations
    float *x; // activation at current time stamp (dim,)
    float *xb; // same, but inside a residual branch (dim,)
    float *xb2; // an additional buffer just for convenience (dim,)
    float *hb; // buffer for hidden dimension in the ffn (hidden_dim,)
    float *hb2; // buffer for hidden dimension in the ffn (hidden_dim,)
    QuantizedTensor xq; // quantized x (dim,)
    QuantizedTensor hq; // quantized hb (hidden_dim,)
    float *q; // query (dim,)
    float *k; // key (dim,)
    float *v; // value (dim,)
    float *att; // buffer for scores/attention values (n_heads, seq_len)
    float *logits; // output logits
    // kv cache
    float* key_cache;   // (layer, seq_len, dim)
    float* value_cache; // (layer, seq_len, dim)
} RunState;

typedef struct {
    Config config; // the hyperparameters of the architecture (the blueprint)
    TransformerWeights weights; // the weights of the model
    RunState state; // buffers for the "wave" of activations in the forward pass
    // some more state needed to properly clean up the memory mapping (sigh)
    float* data; // memory mapped data pointer
} Transformer;

void malloc_run_state(RunState* s, Config* p) {
    // we calloc instead of malloc to keep valgrind happy
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    s->x = (float*)calloc(p->dim, sizeof(float));
    s->xb = (float*)calloc(p->dim, sizeof(float));
    s->xb2 = (float*)calloc(p->dim, sizeof(float));
    s->hb = (float*)calloc(p->hidden_dim, sizeof(float));
    s->hb2 = (float*)calloc(p->hidden_dim, sizeof(float));
    s->xq = (QuantizedTensor) { .q = (int8_t*)calloc(p->dim, sizeof(int8_t)), .s = (float*)calloc(p->dim, sizeof(float)) };
    s->hq = (QuantizedTensor) { .q = (int8_t*)calloc(p->hidden_dim, sizeof(int8_t)), .s = (float*)calloc(p->hidden_dim, sizeof(float)) };
    s->q = (float*)calloc(p->dim, sizeof(float));
    s->k = (float*)calloc(kv_dim, sizeof(float));
    s->v = (float*)calloc(kv_dim, sizeof(float));
    s->att = (float*)calloc(p->n_heads * p->seq_len, sizeof(float));
    s->logits = (float*)calloc(p->vocab_size, sizeof(float));
    s->key_cache = (float*)calloc(p->n_layers * p->seq_len * kv_dim, sizeof(float));
    s->value_cache = (float*)calloc(p->n_layers * p->seq_len * kv_dim, sizeof(float));
    // ensure all mallocs went fine
    /* The four xq/hq pointers allocated just above were once left out of this test, so a
     * failure in one of them slipped through and became a bus fault later in
     * quantize()/matmul(). They are in the list now. */
    if (!s->x || !s->xb || !s->xb2 || !s->hb || !s->hb2 || !s->q
     || !s->k || !s->v || !s->att || !s->logits || !s->key_cache
     || !s->value_cache
     || !s->xq.q || !s->xq.s || !s->hq.q || !s->hq.s) {
        /* exit(EXIT_FAILURE) used to land in llama4micro.cpp's _exit stub, which spins in
         * while(1). T_STORY then never returned from story_init and never disabled the story,
         * so the child was still invited to ask for one. The feature has to be disabled with
         * the failing check named instead. */
        story_model_fail("RUN STATE ALLOC");
        return;
    }
}

void free_run_state(RunState* s) {
    free(s->x);
    free(s->xb);
    free(s->xb2);
    free(s->hb);
    free(s->hb2);
    free(s->xq.q);
    free(s->xq.s);
    free(s->hq.q);
    free(s->hq.s);
    free(s->q);
    free(s->k);
    free(s->v);
    free(s->att);
    free(s->logits);
    free(s->key_cache);
    free(s->value_cache);
}

// ----------------------------------------------------------------------------
// Quantization functions

void dequantize(QuantizedTensor *qx, float* x, int n, int gs) {
    for (int i = 0; i < n; i++) {
        x[i] = qx->q[i] * qx->s[i / gs];
    }
}

void quantize(QuantizedTensor *qx, float* x, int n, int gs) {
    int num_groups = n / gs;
    float Q_MAX = 127.0f;

    for (int group = 0; group < num_groups; group++) {

        // find the max absolute value in the current group
        float wmax = 0.0;
        for (int i = 0; i < gs; i++) {
            float val = fabs(x[group * gs + i]);
            if (val > wmax) {
                wmax = val;
            }
        }

        // calculate and write the scaling factor
        float scale = wmax / Q_MAX;
        if (scale == 0.0f) scale = 1e-9f; // to fix NaN bug
        qx->s[group] = scale;

        // calculate and write the quantized values
        for (int i = 0; i < gs; i++) {
            float quant_value = x[group * gs + i] / scale; // scale
            int8_t quantized = (int8_t) round(quant_value); // round and clamp
            qx->q[group * gs + i] = quantized;
        }
    }
}

/* initialize `n` x quantized tensor (with `size_each` elements), starting from memory pointed at *ptr */
QuantizedTensor *init_quantized_tensors(void **ptr, int n, int size_each, int gs) {
    void *p = *ptr;
    QuantizedTensor *res = (QuantizedTensor*)malloc(n * sizeof(QuantizedTensor));
    /* malloc here is story_malloc (llama4micro.cpp), which returns NULL and latches on
     * failure. Writing res[i].q below without this test was the one place left on the story
     * load path that ended in a bus fault instead of a named, disabled story feature. */
    if (NULL == res) { story_model_fail("WEIGHT TABLE ALLOC"); return NULL; }
    for(int i=0; i<n; i++) {
        /* map quantized int8 values*/
        res[i].q = (int8_t*)p;
        p = (int8_t*)p + size_each;
        /* map scale factors */
        res[i].s = (float*)p;
        p = (float*)p + size_each / gs;
    }
    *ptr = p; // advance ptr to current position
    return res;
}

void memory_map_weights(TransformerWeights *w, Config* p, void* ptr, uint8_t shared_classifier, int gs) {
    int head_size = p->dim / p->n_heads;
    // first are the parameters that are kept in fp32 (the rmsnorm (1D) weights)
    float* fptr = (float*) ptr; // cast our pointer to float*
    w->rms_att_weight = fptr;
    fptr += p->n_layers * p->dim;
    w->rms_ffn_weight = fptr;
    fptr += p->n_layers * p->dim;
    w->rms_final_weight = fptr;
    fptr += p->dim;

    // now read all the quantized weights
    ptr = (void*)fptr; // now cast the pointer back to void*
    /* Every one of the nine calls below is guarded. init_quantized_tensors returns NULL when
     * its small malloc fails, and it has already named the failure.
     * Stopping here leaves the rest of w at whatever it held, which is safe because
     * build_transformer tests story_model_failed() before it sizes the run state, and
     * LoadLlamaModel tests it again before the tokeniser - so nothing ever reads a NULL table.
     * The guards are still written out one by one, so a reader of this function does not have
     * to go and find those two distant gates to see that it is safe. */
    // Token embedding table is NOT dequantized up front. The full fp32 table would
    // need vocab_size*dim*4 bytes (~36.9 MB for stories15M) in SDRAM, but forward()
    // only ever reads one row per token. We keep it quantized in OSPI (w->q_tokens)
    // and dequantize just the needed row on demand in forward(). Saves ~36.9 MB SDRAM.
    /* This is set BEFORE the first guarded call below, so that an early return still leaves it
     * defined - free_transformer() frees it. */
    w->token_embedding_table = NULL;

    w->q_tokens = init_quantized_tensors(&ptr, 1, p->vocab_size * p->dim, gs);
    if (NULL == w->q_tokens) { return; }

    w->wq = init_quantized_tensors(&ptr, p->n_layers, p->dim * (p->n_heads * head_size), gs);
    if (NULL == w->wq) { return; }
    w->wk = init_quantized_tensors(&ptr, p->n_layers, p->dim * (p->n_kv_heads * head_size), gs);
    if (NULL == w->wk) { return; }
    w->wv = init_quantized_tensors(&ptr, p->n_layers, p->dim * (p->n_kv_heads * head_size), gs);
    if (NULL == w->wv) { return; }
    w->wo = init_quantized_tensors(&ptr, p->n_layers, (p->n_heads * head_size) * p->dim, gs);
    if (NULL == w->wo) { return; }

    w->w1 = init_quantized_tensors(&ptr, p->n_layers, p->dim * p->hidden_dim, gs);
    if (NULL == w->w1) { return; }
    w->w2 = init_quantized_tensors(&ptr, p->n_layers, p->hidden_dim * p->dim, gs);
    if (NULL == w->w2) { return; }
    w->w3 = init_quantized_tensors(&ptr, p->n_layers, p->dim * p->hidden_dim, gs);
    if (NULL == w->w3) { return; }

    /* The last one needs no guard after it: there is nothing below it to protect, and a NULL
     * here has already been named by init_quantized_tensors and latched. */
    w->wcls = shared_classifier ? w->q_tokens : init_quantized_tensors(&ptr, 1, p->dim * p->vocab_size, gs);
}

void read_checkpoint(uint8_t* checkpoint_buffer,
                     Config* config, TransformerWeights* weights, float** data, int* group_size) {
    uint8_t* checkpoint_ptr = checkpoint_buffer;
    // read in magic number (uint32), has to be 0x616b3432, i.e. "ak42" in ASCII
    uint32_t magic_number;
    sread(&magic_number, sizeof(uint32_t), &checkpoint_ptr);
    /* THESE TWO CHECKS ARE THE BYTE-ORDER NET. On a failure the story feature is disabled
     * with the failing check named on the console. They used to call exit(EXIT_FAILURE), which
     * spins. */
    if (magic_number != 0x616b3432) { story_model_fail("BAD MAGIC NUMBER"); return; }
    // read in the version number (uint32), has to be 2
    int version;
    sread(&version, sizeof(int), &checkpoint_ptr);
    if (version != 2) { story_model_fail("BAD VERSION"); return; }
    int header_size = 256; // the header size for version 2 in bytes
    // read in the Config
    sread(config, sizeof(Config), &checkpoint_ptr);
    // read in flags
    uint8_t shared_classifier; // a byte to indicate if the classifier is shared
    sread(&shared_classifier, sizeof(uint8_t), &checkpoint_ptr);
    // the group size used in quantization
    sread(group_size, sizeof(int), &checkpoint_ptr);
    // point the Transformer weights at the data pointer
    *data = (float*)checkpoint_buffer;
    void* weights_ptr = ((char*)*data) + header_size; // skip header bytes. char is 1 byte
    memory_map_weights(weights, config, weights_ptr, shared_classifier, *group_size);
}

void build_transformer(Transformer *t,
                       uint8_t* checkpoint_buffer, int* group_size) {
    // read in the Config and the Weights from the checkpoint
    read_checkpoint(checkpoint_buffer, &t->config, &t->weights, &t->data,
                    group_size);

    /* read_checkpoint RETURNS on a failed header check instead of spinning, so t->config holds
     * nothing useful. Allocating the run state from it would ask the heap for a garbage size.
     * Stop here; T_STORY reads story_model_failed() and disables the feature. */
    if (story_model_failed()) { return; }

    // allocate the RunState buffers
    malloc_run_state(&t->state, &t->config);
}

void free_transformer(Transformer* t) {
    // free QuantizedTensors
    free(t->weights.q_tokens);
    free(t->weights.token_embedding_table);
    free(t->weights.wq);
    free(t->weights.wk);
    free(t->weights.wv);
    free(t->weights.wo);
    free(t->weights.w1);
    free(t->weights.w2);
    free(t->weights.w3);
    if(t->weights.wcls != t->weights.q_tokens) { free(t->weights.wcls); }
    // free the RunState buffers
    free_run_state(&t->state);
}

// ----------------------------------------------------------------------------
// neural net blocks; the dynamics of the Transformer

void rmsnorm(float* o, float* x, float* weight, int size) {
    // calculate sum of squares
    float ss = 0.0f;
    for (int j = 0; j < size; j++) {
        ss += x[j] * x[j];
    }
    ss /= size;
    ss += 1e-5f;
    ss = 1.0f / sqrtf(ss);
    // normalize and scale
    for (int j = 0; j < size; j++) {
        o[j] = weight[j] * (ss * x[j]);
    }
}

void softmax(float* x, int size) {
    // find max value (for numerical stability)
    float max_val = x[0];
    for (int i = 1; i < size; i++) {
        if (x[i] > max_val) {
            max_val = x[i];
        }
    }
    // exp and sum
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        x[i] = expf(x[i] - max_val);
        sum += x[i];
    }
    // normalize
    for (int i = 0; i < size; i++) {
        x[i] /= sum;
    }
}

void matmul(float* xout, QuantizedTensor *x, QuantizedTensor *w, int n, int d, int gs) {
    // W (d,n) @ x (n,) -> xout (d,)
    // by far the most amount of time is spent inside this little function
    // inputs to this function are both quantized. w->q streams from OSPI (XiP).

    int i;
    for (i = 0; i < d; i++) {

        float val = 0.0f;
        int in = i * n;

        // do the matmul in groups of GS
        int j;
        for (j = 0; j <= n - gs; j += gs) {
            int32_t ival = 0;
#if LLAMA2_MVE
            // Helium: process 16 int8 lanes per instruction. vmladavaq_s8 accumulates
            // the sum of 16 int8*int8 products straight into a 32-bit scalar.
            const int8_t * px = &x->q[j];
            const int8_t * pw = &w->q[in + j];
            int k = 0;
            for (; k <= gs - 16; k += 16) {
                int8x16_t vx = vld1q_s8(px + k);
                int8x16_t vw = vld1q_s8(pw + k);
                ival = vmladavaq_s8(ival, vx, vw);
            }
            // tail (only runs if gs is not a multiple of 16)
            for (; k < gs; k++) {
                ival += ((int32_t) px[k]) * ((int32_t) pw[k]);
            }
#else
            for (int k = 0; k < gs; k++) {
                ival += ((int32_t) x->q[j + k]) * ((int32_t) w->q[in + j + k]);
            }
#endif
            val += ((float) ival) * w->s[(in + j) / gs] * x->s[j / gs];
        }

        xout[i] = val;
    }
}

float* forward(Transformer* transformer, int token, int pos, int gs) {

    // a few convenience variables
    Config* p = &transformer->config;
    TransformerWeights* w = &transformer->weights;
    RunState* s = &transformer->state;
    float *x = s->x;
    int dim = p->dim;
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    int kv_mul = p->n_heads / p->n_kv_heads; // integer multiplier of the kv sharing in multiquery
    int hidden_dim =  p->hidden_dim;
    int head_size = dim / p->n_heads;

    // Dequantize just this token's embedding row into x, on demand, straight from
    // the quantized table in OSPI (no 36.9 MB fp32 table in SDRAM). Mirrors dequantize().
    {
        QuantizedTensor *qt = w->q_tokens;
        int base = token * dim;
        for (int i = 0; i < dim; i++) {
            x[i] = qt->q[base + i] * qt->s[(base + i) / gs];
        }
    }

    // forward all the layers
    for(int l = 0; l < p->n_layers; l++) {

        // attention rmsnorm
        rmsnorm(s->xb, x, w->rms_att_weight + l*dim, dim);

        // qkv matmuls for this position
        quantize(&s->xq, s->xb, dim, gs);
        matmul(s->q, &s->xq, w->wq + l, dim, dim, gs);
        matmul(s->k, &s->xq, w->wk + l, dim, kv_dim, gs);
        matmul(s->v, &s->xq, w->wv + l, dim, kv_dim, gs);

        // RoPE relative positional encoding: complex-valued rotate q and k in each head
        for (int i = 0; i < dim; i+=2) {
            int head_dim = i % head_size;
            float freq = 1.0f / powf(10000.0f, head_dim / (float)head_size);
            float val = pos * freq;
            float fcr = cosf(val);
            float fci = sinf(val);
            int rotn = i < kv_dim ? 2 : 1; // how many vectors? 2 = q & k, 1 = q only
            for (int v = 0; v < rotn; v++) {
                float* vec = v == 0 ? s->q : s->k; // the vector to rotate (query or key)
                float v0 = vec[i];
                float v1 = vec[i+1];
                vec[i]   = v0 * fcr - v1 * fci;
                vec[i+1] = v0 * fci + v1 * fcr;
            }
        }

        // save key,value at this time step (pos) to our kv cache
        int loff = l * p->seq_len * kv_dim; // kv cache layer offset for convenience
        float* key_cache_row = s->key_cache + loff + pos * kv_dim;
        float* value_cache_row = s->value_cache + loff + pos * kv_dim;
        memcpy(key_cache_row, s->k, kv_dim * sizeof(*key_cache_row));
        memcpy(value_cache_row, s->v, kv_dim * sizeof(*value_cache_row));

        // multihead attention. iterate over all heads
        int h;
        for (h = 0; h < p->n_heads; h++) {
            // get the query vector for this head
            float* q = s->q + h * head_size;
            // attention scores for this head
            float* att = s->att + h * p->seq_len;
            // iterate over all timesteps, including the current one
            for (int t = 0; t <= pos; t++) {
                // get the key vector for this head and at this timestep
                float* k = s->key_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                // calculate the attention score as the dot product of q and k
                float score = 0.0f;
                for (int i = 0; i < head_size; i++) {
                    score += q[i] * k[i];
                }
                score /= sqrtf(head_size);
                // save the score to the attention buffer
                att[t] = score;
            }

            // softmax the scores to get attention weights, from 0..pos inclusively
            softmax(att, pos + 1);

            // weighted sum of the values, store back into xb
            float* xb = s->xb + h * head_size;
            memset(xb, 0, head_size * sizeof(float));
            for (int t = 0; t <= pos; t++) {
                // get the value vector for this head and at this timestep
                float* v = s->value_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                // get the attention weight for this timestep
                float a = att[t];
                // accumulate the weighted value into xb
                for (int i = 0; i < head_size; i++) {
                    xb[i] += a * v[i];
                }
            }
        }

        // final matmul to get the output of the attention
        quantize(&s->xq, s->xb, dim, gs);
        matmul(s->xb2, &s->xq, w->wo + l, dim, dim, gs);

        // residual connection back into x
        for (int i = 0; i < dim; i++) {
            x[i] += s->xb2[i];
        }

        // ffn rmsnorm
        rmsnorm(s->xb, x, w->rms_ffn_weight + l*dim, dim);

        // Now for FFN in PyTorch we have: self.w2(F.silu(self.w1(x)) * self.w3(x))
        // first calculate self.w1(x) and self.w3(x)
        quantize(&s->xq, s->xb, dim, gs);
        matmul(s->hb, &s->xq, w->w1 + l, dim, hidden_dim, gs);
        matmul(s->hb2, &s->xq, w->w3 + l, dim, hidden_dim, gs);

        // SwiGLU non-linearity
        for (int i = 0; i < hidden_dim; i++) {
            float val = s->hb[i];
            // silu(x)=x*σ(x), where σ(x) is the logistic sigmoid
            val *= (1.0f / (1.0f + expf(-val)));
            // elementwise multiply with w3(x)
            val *= s->hb2[i];
            s->hb[i] = val;
        }

        // final matmul to get the output of the ffn
        quantize(&s->hq, s->hb, hidden_dim, gs);
        matmul(s->xb, &s->hq, w->w2 + l, hidden_dim, dim, gs);

        // residual connection
        for (int i = 0; i < dim; i++) {
            x[i] += s->xb[i];
        }
    }

    // final rmsnorm
    rmsnorm(x, x, w->rms_final_weight, dim);

    // classifier into logits
    quantize(&s->xq, x, dim, gs);
    matmul(s->logits, &s->xq, w->wcls, dim, p->vocab_size, gs);
    return s->logits;
}

// ----------------------------------------------------------------------------
// The Byte Pair Encoding (BPE) Tokenizer that translates strings <-> tokens

typedef struct {
    const char *str;
    int id;
} TokenIndex;

typedef struct {
    char** vocab;
    char*  vocab_blob;   /* EDIT 6: one block holding all 32,000 token strings end to end */
    float* vocab_scores;
    TokenIndex *sorted_vocab;
    int vocab_size;
    unsigned int max_token_length;
    unsigned char byte_pieces[512]; // stores all single-byte strings
} Tokenizer;

int compare_tokens(const void *a, const void *b) {
    return strcmp(((TokenIndex*)a)->str, ((TokenIndex*)b)->str);
}

/* ---------------------------------------------------------------------------------------------
 * ONE BLOCK FOR THE WHOLE TOKENISER BLOB.
 *
 * WHAT CHANGED. The loop below used to take one small block per token: 32,000 calls to malloc,
 * one per vocabulary string. Now ONE block the size of the tokeniser blob is taken and the
 * strings are copied into it end to end, with t->vocab pointing inside that block. The two
 * 128,000-byte arrays for the pointers and the scores are unchanged.
 *
 * WHY. The allocation count for the story side falls from about 32,020 to about 20, which is
 * what keeps the heap from fragmenting and what makes the Stage 2 memory pool workable.
 *
 * WHAT IT COSTS. One block of sizeof(tokenizer_bin) = 433,870 bytes against about 209,865 bytes
 * of actual string content, so it ADDS about 224,000 bytes before counting back the roughly
 * 32,000 block headers it removes. That is the simple version: no pre-pass over the blob, and
 * A SIZE THAT CANNOT BE WRONG. The 8,388,608-byte pool has room.
 *
 * THE SIGNATURE GAINS ONE ARGUMENT, blob_bytes, because that "size that cannot be wrong" is
 * sizeof(tokenizer_bin), and tokenizer.h is included after this file by llama4micro.cpp, so
 * this function cannot read it for itself. There is exactly one caller.
 *
 * TO REVERT: delete the block allocation, put the '#if 0' loop below back, and drop the
 * blob_bytes argument and Tokenizer::vocab_blob.
 * ------------------------------------------------------------------------------------------ */
void build_tokenizer(Tokenizer* t,
                     uint8_t* tokenizer_buffer, int vocab_size, size_t blob_bytes) {
    // i should have written the vocab_size into the tokenizer file... sigh
    t->vocab_size = vocab_size;
    // malloc space to hold the scores and the strings
    t->vocab = (char**)malloc(vocab_size * sizeof(char*));
    t->vocab_scores = (float*)malloc(vocab_size * sizeof(float));
    t->vocab_blob = (char*)malloc(blob_bytes);
    t->sorted_vocab = NULL; // initialized lazily
    for (int i = 0; i < 256; i++) {
        t->byte_pieces[i * 2] = (unsigned char)i;
        t->byte_pieces[i * 2 + 1] = '\0';
    }

    /* story_malloc returns NULL when the heap is out, latches the failure and reports it.
     * Nothing may be written through a NULL pointer here; T_STORY tests story_alloc_failed()
     * straight after story_init() and switches the story feature off. */
    if (t->vocab == NULL || t->vocab_scores == NULL || t->vocab_blob == NULL) {
        t->vocab_size = 0;
        return;
    }

    uint8_t* tokenizer_ptr = tokenizer_buffer;
    sread(&t->max_token_length, sizeof(int), &tokenizer_ptr);
    int len;
    char* next = t->vocab_blob;
    for (int i = 0; i < vocab_size; i++) {
        sread(t->vocab_scores + i, sizeof(float), &tokenizer_ptr);
        sread(&len, sizeof(int), &tokenizer_ptr);
        t->vocab[i] = next;
        sread(next, len, &tokenizer_ptr);
        next[len] = '\0'; // add the string terminating token
        next += len + 1;
    }
#if 0
    /* The old one-block-per-token loop, kept for the revert described above. It is the only
     * thing this edit replaced. */
    for (int i = 0; i < vocab_size; i++) {
        sread(t->vocab_scores + i, sizeof(float), &tokenizer_ptr);
        sread(&len, sizeof(int), &tokenizer_ptr);
        t->vocab[i] = (char *)malloc(len + 1);
        sread(t->vocab[i], len, &tokenizer_ptr);
        t->vocab[i][len] = '\0'; // add the string terminating token
    }
#endif
}

void free_tokenizer(Tokenizer* t) {
    /* EDIT 6: one release instead of 32,000. */
    free(t->vocab_blob);
    free(t->vocab);
    free(t->vocab_scores);
    free(t->sorted_vocab);
}

char* decode(Tokenizer* t, int prev_token, int token) {
    char *piece = t->vocab[token];
    // following BOS (1) token, sentencepiece decoder strips any leading whitespace (see PR #89)
    if (prev_token == 1 && piece[0] == ' ') { piece++; }
    // careful, some tokens designate raw bytes, and look like e.g. '<0x01>'
    // parse this and convert and return the actual byte
    unsigned char byte_val;
    if (sscanf(piece, "<0x%02hhX>", &byte_val) == 1) {
        piece = (char*)t->byte_pieces + byte_val * 2;
    }
    return piece;
}

/* ---------------------------------------------------------------------------------------------
 * The fork's safe_printf becomes story_emit, which sends every piece of text TO BOTH the
 * screen and the console, because the demo needs both: the three rolling lines on the panel,
 * and the whole story on the serial terminal, one piece at a time, exactly as the sample does
 * today.
 *
 * The unsafe-byte filter is the fork's own and is unchanged: a raw byte token that is neither
 * printable nor whitespace is dropped, so a control code never reaches the terminal or the font
 * lookup.
 *
 * TO REVERT: rename it back to safe_printf and make the last two lines printf("%s", piece).
 * ------------------------------------------------------------------------------------------ */
void story_emit(char *piece) {
    // piece might be a raw byte token, and we only want to print printable chars or whitespace
    // because some of the other bytes can be various control codes, backspace, etc.
    if (piece == NULL) { return; }
    if (piece[0] == '\0') { return; }
    if (piece[1] == '\0') {
        unsigned char byte_val = piece[0];
        if (!(isprint(byte_val) || isspace(byte_val))) {
            return; // bad byte, don't print it
        }
    }
    story_text_put(piece);      /* the screen: T_UI drains this ring buffer once per frame */
    print_to_console(piece);    /* the terminal, exactly as the fork did                   */
}

/* True when this piece of text ends a sentence. Used by edit 5 below. */
static int story_piece_ends_sentence(const char *piece) {
    size_t n;
    if (piece == NULL) { return 0; }
    n = strlen(piece);
    while (n > 0 && (piece[n - 1] == ' ' || piece[n - 1] == '\n')) { n--; }
    if (n == 0) { return 0; }
    return (piece[n - 1] == '.' || piece[n - 1] == '!' || piece[n - 1] == '?');
}

int str_lookup(const char *str, TokenIndex *sorted_vocab, int vocab_size) {
    // efficiently find the perfect match for str in vocab, return its index or -1 if not found
    TokenIndex tok = { .str = str }; // acts as the key to search for
    TokenIndex *res = (TokenIndex*)bsearch(&tok, sorted_vocab, vocab_size, sizeof(TokenIndex), compare_tokens);
    return res != NULL ? res->id : -1;
}

void encode(Tokenizer* t, const char *text, int8_t bos, int8_t eos, int *tokens, int *n_tokens) {
    // encode the string text (input) into an upper-bound preallocated tokens[] array
    // bos != 0 means prepend the BOS token (=1), eos != 0 means append the EOS token (=2)
    /* The fourth and last exit() on the story path. Nothing in this demo passes NULL -
     * generate() swaps a NULL prompt for an empty string - but an exit() left here would still
     * be a spin waiting to happen. */
    if (text == NULL) { *n_tokens = 0; story_model_fail("NULL PROMPT TEXT"); return; }

    if (t->sorted_vocab == NULL) {
        // lazily malloc and sort the vocabulary
        t->sorted_vocab = (TokenIndex*)malloc(t->vocab_size * sizeof(TokenIndex));
        for (int i = 0; i < t->vocab_size; i++) {
            t->sorted_vocab[i].str = t->vocab[i];
            t->sorted_vocab[i].id = i;
        }
        qsort(t->sorted_vocab, t->vocab_size, sizeof(TokenIndex), compare_tokens);
    }

    // create a temporary buffer that will store merge candidates of always two consecutive tokens
    // *2 for concat, +1 for null terminator +2 for UTF8 (in case max_token_length is 1)
    char* str_buffer = (char*)malloc((t->max_token_length*2 +1 +2) * sizeof(char));
    size_t str_len = 0;

    // start at 0 tokens
    *n_tokens = 0;

    // add optional BOS (=1) token, if desired
    if (bos) tokens[(*n_tokens)++] = 1;

    // add_dummy_prefix is true by default
    // so prepend a dummy prefix token to the input string, but only if text != ""
    // TODO: pretty sure this isn't correct in the general case but I don't have the
    // energy to read more of the sentencepiece code to figure out what it's doing
    if (text[0] != '\0') {
        int dummy_prefix = str_lookup(" ", t->sorted_vocab, t->vocab_size);
        tokens[(*n_tokens)++] = dummy_prefix;
    }

    // Okay UTF-8 time. This will get messy. Here is the reference from Wikipedia:
    // Code point ↔ UTF-8 conversion
    // First code point	Last code point	Byte 1	Byte 2	Byte 3	Byte 4
    // U+0000	U+007F	    0xxxxxxx
    // U+0080	U+07FF	    110xxxxx	10xxxxxx
    // U+0800	U+FFFF	    1110xxxx	10xxxxxx	10xxxxxx
    // U+10000	U+10FFFF    11110xxx	10xxxxxx	10xxxxxx	10xxxxxx

    // process the raw (UTF-8) byte sequence of the input string
    for (const char *c = text; *c != '\0'; c++) {

        // reset buffer if the current byte is ASCII or a leading byte
        // 0xC0 is 11000000, so (*c & 0xC0) keeps the first 2 bits and zeros the rest
        // 0x80 is 10000000
        // in UTF-8, all continuation bytes start with "10" in first two bits
        // so in English this is: "if this byte is not a continuation byte"
        if ((*c & 0xC0) != 0x80) {
            // this byte must be either a leading byte (11...) or an ASCII char (0x...)
            // => reset our location, as we're starting a new UTF-8 codepoint
            str_len = 0;
        }

        // append the current byte to the buffer
        str_buffer[str_len++] = *c; // ++ is post-increment, incremented after this line
        str_buffer[str_len] = '\0';

        // while the next character is a continuation byte, continue appending
        // but if there are too many of them, just stop to avoid overruning str_buffer size.
        if ((*(c+1) & 0xC0) == 0x80 && str_len < 4) {
            continue;
        }

        // ok c+1 is not a continuation byte, so we've read in a full codepoint
        int id = str_lookup(str_buffer, t->sorted_vocab, t->vocab_size);

        if (id != -1) {
            // we found this codepoint in vocab, add it as a token
            tokens[(*n_tokens)++] = id;
        } else {
            // byte_fallback encoding: just encode each byte as a token
            // +3 is here because the first 3 vocab elements are <unk>, <s>, </s>
            // so the individual bytes only start at index 3
            for (size_t i=0; i < str_len; i++) {
                tokens[(*n_tokens)++] = (unsigned char)str_buffer[i] + 3;
            }
        }
        str_len = 0; // protect against a sequence of stray UTF8 continuation bytes
    }

    // merge the best consecutive pair each iteration, according the scores in vocab_scores
    while (1) {
        float best_score = -1e10;
        int best_id = -1;
        int best_idx = -1;

        for (int i=0; i < (*n_tokens-1); i++) {
            // check if we can merge the pair (tokens[i], tokens[i+1])
            sprintf(str_buffer, "%s%s", t->vocab[tokens[i]], t->vocab[tokens[i+1]]);
            int id = str_lookup(str_buffer, t->sorted_vocab, t->vocab_size);
            if (id != -1 && t->vocab_scores[id] > best_score) {
                // this merge pair exists in vocab! record its score and position
                best_score = t->vocab_scores[id];
                best_id = id;
                best_idx = i;
            }
        }

        if (best_idx == -1) {
            break; // we couldn't find any more pairs to merge, so we're done
        }

        // merge the consecutive pair (best_idx, best_idx+1) into new token best_id
        tokens[best_idx] = best_id;
        // delete token at position best_idx+1, shift the entire sequence back 1
        for (int i = best_idx+1; i < (*n_tokens-1); i++) {
            tokens[i] = tokens[i+1];
        }
        (*n_tokens)--; // token length decreased
    }

    // add optional EOS (=2) token, if desired
    if (eos) tokens[(*n_tokens)++] = 2;

    free(str_buffer);
}

// ----------------------------------------------------------------------------
// The Sampler, which takes logits and returns a sampled token
// sampling can be done in a few ways: greedy argmax, sampling, top-p sampling

typedef struct {
    float prob;
    int index;
} ProbIndex; // struct used when sorting probabilities during top-p sampling

typedef struct {
    int vocab_size;
    ProbIndex* probindex; // buffer used in top-p sampling
    float temperature;
    float topp;
    unsigned long long rng_state;
} Sampler;

int sample_argmax(float* probabilities, int n) {
    // return the index that has the highest probability
    int max_i = 0;
    float max_p = probabilities[0];
    for (int i = 1; i < n; i++) {
        if (probabilities[i] > max_p) {
            max_i = i;
            max_p = probabilities[i];
        }
    }
    return max_i;
}

int sample_mult(float* probabilities, int n, float coin) {
    // sample index from probabilities (they must sum to 1!)
    // coin is a random number in [0, 1), usually from random_f32()
    float cdf = 0.0f;
    for (int i = 0; i < n; i++) {
        cdf += probabilities[i];
        if (coin < cdf) {
            return i;
        }
    }
    return n - 1; // in case of rounding errors
}

int compare(const void* a, const void* b) {
    ProbIndex* a_ = (ProbIndex*) a;
    ProbIndex* b_ = (ProbIndex*) b;
    if (a_->prob > b_->prob) return -1;
    if (a_->prob < b_->prob) return 1;
    return 0;
}

int sample_topp(float* probabilities, int n, float topp, ProbIndex* probindex, float coin) {
    // top-p sampling (or "nucleus sampling") samples from the smallest set of
    // tokens that exceed probability topp. This way we never sample tokens that
    // have very low probabilities and are less likely to go "off the rails".
    // coin is a random number in [0, 1), usually from random_f32()

    int n0 = 0;
    // quicksort indices in descending order of probabilities
    // values smaller than (1 - topp) / (n - 1) cannot be part of the result
    // so for efficiency we crop these out as candidates before sorting
    const float cutoff = (1.0f - topp) / (n - 1);
    for (int i = 0; i < n; i++) {
        if (probabilities[i] >= cutoff) {
            probindex[n0].index = i;
            probindex[n0].prob = probabilities[i];
            n0++;
        }
    }
    qsort(probindex, n0, sizeof(ProbIndex), compare);

    // truncate the list where cumulative probability exceeds topp
    float cumulative_prob = 0.0f;
    int last_idx = n0 - 1; // in case of rounding errors consider all elements
    for (int i = 0; i < n0; i++) {
        cumulative_prob += probindex[i].prob;
        if (cumulative_prob > topp) {
            last_idx = i;
            break; // we've exceeded topp by including last_idx
        }
    }

    // sample from the truncated list
    float r = coin * cumulative_prob;
    float cdf = 0.0f;
    for (int i = 0; i <= last_idx; i++) {
        cdf += probindex[i].prob;
        if (r < cdf) {
            return probindex[i].index;
        }
    }
    return probindex[last_idx].index; // in case of rounding errors
}

void build_sampler(Sampler* sampler, int vocab_size, float temperature, float topp, unsigned long long rng_seed) {
    sampler->vocab_size = vocab_size;
    sampler->temperature = temperature;
    sampler->topp = topp;
    sampler->rng_state = rng_seed;
    // buffer only used with nucleus sampling; may not need but it's ~small
    sampler->probindex = (ProbIndex*)malloc(sampler->vocab_size * sizeof(ProbIndex));
}

void free_sampler(Sampler* sampler) {
    free(sampler->probindex);
}

unsigned int random_u32(unsigned long long *state) {
    // xorshift rng: https://en.wikipedia.org/wiki/Xorshift#xorshift.2A
    *state ^= *state >> 12;
    *state ^= *state << 25;
    *state ^= *state >> 27;
    return (*state * 0x2545F4914F6CDD1Dull) >> 32;
}

float random_f32(unsigned long long *state) { // random float32 in [0,1)
    return (random_u32(state) >> 8) / 16777216.0f;
}

int sample(Sampler* sampler, float* logits) {
    // sample the token given the logits and some hyperparameters
    int next;
    if (sampler->temperature == 0.0f) {
        // greedy argmax sampling: take the token with the highest probability
        next = sample_argmax(logits, sampler->vocab_size);
    } else {
        // apply the temperature to the logits
        for (int q=0; q<sampler->vocab_size; q++) { logits[q] /= sampler->temperature; }
        // apply softmax to the logits to get the probabilities for next token
        softmax(logits, sampler->vocab_size);
        // flip a (float) coin (this is our source of entropy for sampling)
        float coin = random_f32(&sampler->rng_state);
        // we sample from this distribution to get the next token
        if (sampler->topp <= 0 || sampler->topp >= 1) {
            // simply sample from the predicted probability distribution
            next = sample_mult(logits, sampler->vocab_size, coin);
        } else {
            // top-p (nucleus) sampling, clamping the least likely tokens to zero
            next = sample_topp(logits, sampler->vocab_size, sampler->topp, sampler->probindex, coin);
        }
    }
    return next;
}

// ----------------------------------------------------------------------------
// generation loop

/* The out parameter used to be "float* tokens_s", and nothing ever wrote it - TellStory worked
 * the rate out from `steps`, the 128-token ceiling, even though this loop leaves early three
 * ways: the end-of-sequence token, the sentence-end rule after token 96, and
 * story_abandon_requested(). An early stop is the NORMAL case, so the printed rate was
 * over-reported by up to about a third.
 *
 * The parameter now carries the count of tokens actually produced, and TellStory divides by it. */
void generate(Transformer *transformer, Tokenizer *tokenizer, Sampler *sampler, const char *prompt,
              int steps, int GA, int* out_tokens) {
    const char *empty_prompt = "";
    if (prompt == NULL) { prompt = empty_prompt; }

    if (out_tokens != NULL) { *out_tokens = 0; }

    // encode the (string) prompt into tokens sequence
    int num_prompt_tokens = 0;
    int* prompt_tokens = (int*)malloc((strlen(prompt)+3) * sizeof(int)); // +3 for '\0', ?BOS, ?EOS
    if (prompt_tokens == NULL) { story_model_fail("PROMPT BUFFER"); return; }

    encode(tokenizer, prompt, 1, 0, prompt_tokens, &num_prompt_tokens);
    if (num_prompt_tokens < 1) {
        /* The third exit() on the story path. */
        story_model_fail("PROMPT ENCODE");
        free(prompt_tokens);
        return;
    }

    // start the main loop
    // "long start" is deleted. It was the upstream timer, and the timing moved out to
    // TellStory, which brackets this call with
    // TimeCounter_CurrentCountGet(). Nothing in this function ever read it, so it was only a
    // warning ("unused variable 'start'", 2026-08-14 build log) and a false trail for a reader
    // looking for where the elapsed time comes from.
    int next;        // will store the next token in the sequence
    int token = prompt_tokens[0]; // kick off with the first token in the prompt
    int pos = 0;     // position in the sequence
    while (pos < steps) {

        // forward the transformer to get logits for the next token
        float* logits = forward(transformer, token, pos, GA);

        // advance the state state machine
        if (pos < num_prompt_tokens - 1) {
            // if we are still processing the input prompt, force the next prompt token
            next = prompt_tokens[pos + 1];
        } else {
            // otherwise sample the next token from the logits
            next = sample(sampler, logits);
        }
        pos++;

        // data-dependent terminating condition: the BOS (=1) token delimits sequences
        if (next == 1) { break; }

        // print the token as string, decode it with the Tokenizer object
        char* piece = decode(tokenizer, token, next);
        story_emit(piece); // EDIT 4: to the screen AND the console, unsafe bytes still skipped
        fflush(stdout);
        token = next;

        /* THE SENTENCE-END STOP, an addition of the demo's own: after token
         * STORY_SENTENCE_STOP_POS the story stops at the first piece that ends a sentence, so a
         * story that runs to the 128-token ceiling rarely stops in the middle of a word. The
         * generator's own end-of-sequence stop above is untouched.
         * TO REVERT: delete this if. */
        if (pos > STORY_SENTENCE_STOP_POS && story_piece_ends_sentence(piece)) { break; }

        /* T_UI gave up on this story after fifteen seconds with no new text, or the booth reset
         * was pressed (story_abandon in src\tasks\task_story.c). story_abandon only sets the
         * wish; this is the one place inside the token loop that can act on it. */
        if (story_abandon_requested()) { break; }
    }
    printf("\n");

    /* pos is the number of tokens this call really produced, whichever of the four ways out
     * the loop took. */
    if (out_tokens != NULL) { *out_tokens = pos; }

    free(prompt_tokens);
}
