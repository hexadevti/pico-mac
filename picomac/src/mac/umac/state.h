/* pico-mac: machine snapshots. Each module moves its state through one function used both ways:
 * io copies n bytes from p to the stream when saving, or from the stream into p when loading.
 * The first failure sticks in err and the rest is skipped.
 */
#ifndef UMAC_STATE_H
#define UMAC_STATE_H

typedef int (*umac_state_io)(void *ctx, void *p, unsigned int n);   /* nonzero = failed */

typedef struct umac_state {
        umac_state_io io;
        void *ctx;
        int loading;
        int err;
} umac_state_t;

static inline void umac_st(umac_state_t *s, void *p, unsigned int n)
{
        if (!s->err && s->io(s->ctx, p, n))
                s->err = 1;
}

#define UMAC_ST(s, v)   umac_st((s), &(v), sizeof(v))

void    via_state(umac_state_t *s);
void    scc_state(umac_state_t *s);
void    disc_state(umac_state_t *s);

#endif
