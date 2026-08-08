/* Compact NMOS 6502 core written for a800. Decimal arithmetic and the JMP
 * indirect page-wrap quirk are implemented; undocumented opcodes act as NOPs.
 */
#include "6502.h"

enum { C = 1, Z = 2, I = 4, D = 8, B = 16, U = 32, V = 64, N = 128 };

static uint8_t rd(Atari *m, uint16_t a) { return Atari_Read(m, a); }
static void wr(Atari *m, uint16_t a, uint8_t v) { Atari_Write(m, a, v); }
static uint8_t fetch(Atari *m) { return rd(m, m->pc++); }
static uint16_t word(Atari *m) {
  uint8_t l = fetch(m);
  return (uint16_t)(l | fetch(m) << 8);
}
static void nz(Atari *m, uint8_t v) {
  m->p = (uint8_t)((m->p & ~(N | Z)) | (v ? 0 : Z) | (v & N));
}
static void push(Atari *m, uint8_t v) { wr(m, (uint16_t)(0x100 | m->s--), v); }
static uint8_t pull(Atari *m) { return rd(m, (uint16_t)(0x100 | ++m->s)); }
static uint16_t zp(Atari *m) { return fetch(m); }
static uint16_t zpx(Atari *m) { return (uint8_t)(fetch(m) + m->x); }
static uint16_t zpy(Atari *m) { return (uint8_t)(fetch(m) + m->y); }
static uint16_t ax(Atari *m) { return (uint16_t)(word(m) + m->x); }
static uint16_t ay(Atari *m) { return (uint16_t)(word(m) + m->y); }
static uint16_t ix(Atari *m) {
  uint8_t z = (uint8_t)(fetch(m) + m->x);
  return (uint16_t)(rd(m, z) | rd(m, (uint8_t)(z + 1)) << 8);
}
static uint16_t iy(Atari *m) {
  uint8_t z = fetch(m);
  return (uint16_t)((rd(m, z) | rd(m, (uint8_t)(z + 1)) << 8) + m->y);
}
static void cmp(Atari *m, uint8_t r, uint8_t v) {
  uint16_t q = (uint16_t)r - v;
  m->p = (uint8_t)((m->p & ~C) | (r >= v ? C : 0));
  nz(m, (uint8_t)q);
}
static void adc(Atari *m, uint8_t v) {
  unsigned s;
  if (m->p & D) {
    unsigned lo = (m->a & 15) + (v & 15) + (m->p & C),
             hi = (m->a >> 4) + (v >> 4);
    if (lo > 9) {
      lo += 6;
      hi++;
    }
    if (hi > 9)
      hi += 6;
    s = (hi << 4) | (lo & 15);
  } else
    s = m->a + v + (m->p & C);
  m->p = (uint8_t)((m->p & ~(C | V)) | (s > 255 ? C : 0) |
                   ((~(m->a ^ v) & (m->a ^ (uint8_t)s) & 0x80) ? V : 0));
  m->a = (uint8_t)s;
  nz(m, m->a);
}
static void sbc(Atari *m, uint8_t v) {
  if (m->p & D) {
    int lo = (m->a & 15) - (v & 15) - (1 - (m->p & C)),
        hi = (m->a >> 4) - (v >> 4);
    if (lo < 0) {
      lo -= 6;
      hi--;
    }
    if (hi < 0)
      hi -= 6;
    uint8_t r = (uint8_t)((hi << 4) | (lo & 15));
    m->p =
        (uint8_t)((m->p & ~(C | V)) | (m->a >= v + (1 - (m->p & C)) ? C : 0) |
                  (((m->a ^ v) & (m->a ^ r) & 128) ? V : 0));
    m->a = r;
  } else
    adc(m, (uint8_t)~v);
  nz(m, m->a);
}
static void interrupt(Atari *m, uint16_t vector, int brk) {
  push(m, (uint8_t)(m->pc >> 8));
  push(m, (uint8_t)m->pc);
  push(m, (uint8_t)((m->p & ~B) | U | (brk ? B : 0)));
  m->p |= I;
  m->pc = (uint16_t)(rd(m, vector) | rd(m, (uint16_t)(vector + 1)) << 8);
}
void Cpu_Reset(Atari *m) {
  m->a = m->x = m->y = 0;
  m->s = 0xfd;
  m->p = I | U;
  m->pc = (uint16_t)(rd(m, 0xfffc) | rd(m, 0xfffd) << 8);
  m->stopped = 0;
}
void Cpu_Nmi(Atari *m) { interrupt(m, 0xfffa, 0); }
void Cpu_Irq(Atari *m) {
  if (!(m->p & I))
    interrupt(m, 0xfffe, 0);
}

#define LOAD(reg, addr)                                                        \
  do {                                                                         \
    (reg) = rd(m, (addr));                                                     \
    nz(m, (reg));                                                              \
  } while (0)
#define STORE(reg, addr) wr(m, (addr), (reg))
#define RMW(addr, expr)                                                        \
  do {                                                                         \
    uint16_t q = (addr);                                                       \
    uint8_t v = rd(m, q);                                                      \
    v = (expr);                                                                \
    wr(m, q, v);                                                               \
    nz(m, v);                                                                  \
  } while (0)
#define BRANCH(test)                                                           \
  do {                                                                         \
    int8_t d = (int8_t)fetch(m);                                               \
    if (test) {                                                                \
      m->pc = (uint16_t)(m->pc + d);                                           \
      c++;                                                                     \
    }                                                                          \
  } while (0)

int Cpu_Step(Atari *m) {
  uint8_t op;
  uint16_t q;
  int c = 2;
  if (m->nmi) {
    m->nmi = 0;
    Cpu_Nmi(m);
    return 7;
  }
  if (m->irq && !(m->p & I)) {
    Cpu_Irq(m);
    return 7;
  }
  if (m->stopped)
    return 1;
  op = fetch(m);
  switch (op) {
  case 0x00:
    m->pc++;
    interrupt(m, 0xfffe, 1);
    c = 7;
    break;
  case 0x01:
    m->a |= rd(m, ix(m));
    nz(m, m->a);
    c = 6;
    break;
  case 0x05:
    m->a |= rd(m, zp(m));
    nz(m, m->a);
    c = 3;
    break;
  case 0x06:
    RMW(zp(m), (uint8_t)((v << 1) | ((m->p = (m->p & ~C) | (v >> 7)), 0)));
    c = 5;
    break;
  case 0x08:
    push(m, m->p | B | U);
    c = 3;
    break;
  case 0x09:
    m->a |= fetch(m);
    nz(m, m->a);
    break;
  case 0x0a:
    m->p = (m->p & ~C) | (m->a >> 7);
    m->a <<= 1;
    nz(m, m->a);
    break;
  case 0x0d:
    m->a |= rd(m, word(m));
    nz(m, m->a);
    c = 4;
    break;
  case 0x0e:
    RMW(word(m), (uint8_t)((v << 1) | ((m->p = (m->p & ~C) | (v >> 7)), 0)));
    c = 6;
    break;
  case 0x10:
    BRANCH(!(m->p & N));
    break;
  case 0x11:
    m->a |= rd(m, iy(m));
    nz(m, m->a);
    c = 5;
    break;
  case 0x15:
    m->a |= rd(m, zpx(m));
    nz(m, m->a);
    c = 4;
    break;
  case 0x16:
    RMW(zpx(m), (uint8_t)((v << 1) | ((m->p = (m->p & ~C) | (v >> 7)), 0)));
    c = 6;
    break;
  case 0x18:
    m->p &= ~C;
    break;
  case 0x19:
    m->a |= rd(m, ay(m));
    nz(m, m->a);
    c = 4;
    break;
  case 0x1d:
    m->a |= rd(m, ax(m));
    nz(m, m->a);
    c = 4;
    break;
  case 0x1e:
    RMW(ax(m), (uint8_t)((v << 1) | ((m->p = (m->p & ~C) | (v >> 7)), 0)));
    c = 7;
    break;
  case 0x20:
    q = word(m);
    m->pc--;
    push(m, m->pc >> 8);
    push(m, (uint8_t)m->pc);
    m->pc = q;
    c = 6;
    break;
  case 0x21:
    m->a &= rd(m, ix(m));
    nz(m, m->a);
    c = 6;
    break;
  case 0x24:
    q = rd(m, zp(m));
    m->p = (m->p & ~(N | V | Z)) | (q & (N | V)) | ((m->a & q) ? 0 : Z);
    c = 3;
    break;
  case 0x25:
    m->a &= rd(m, zp(m));
    nz(m, m->a);
    c = 3;
    break;
  case 0x26:
    q = zp(m);
    {
      uint8_t v = rd(m, q), o = m->p & C;
      m->p = (m->p & ~C) | (v >> 7);
      v = (v << 1) | o;
      wr(m, q, v);
      nz(m, v);
    }
    c = 5;
    break;
  case 0x28:
    m->p = (pull(m) & ~B) | U;
    c = 4;
    break;
  case 0x29:
    m->a &= fetch(m);
    nz(m, m->a);
    break;
  case 0x2a: {
    uint8_t o = m->p & C;
    m->p = (m->p & ~C) | (m->a >> 7);
    m->a = (m->a << 1) | o;
    nz(m, m->a);
  } break;
  case 0x2c:
    q = rd(m, word(m));
    m->p = (m->p & ~(N | V | Z)) | (q & (N | V)) | ((m->a & q) ? 0 : Z);
    c = 4;
    break;
  case 0x2d:
    m->a &= rd(m, word(m));
    nz(m, m->a);
    c = 4;
    break;
  case 0x2e:
    q = word(m);
    {
      uint8_t v = rd(m, q), o = m->p & C;
      m->p = (m->p & ~C) | (v >> 7);
      v = (v << 1) | o;
      wr(m, q, v);
      nz(m, v);
    }
    c = 6;
    break;
  case 0x30:
    BRANCH(m->p & N);
    break;
  case 0x31:
    m->a &= rd(m, iy(m));
    nz(m, m->a);
    c = 5;
    break;
  case 0x35:
    m->a &= rd(m, zpx(m));
    nz(m, m->a);
    c = 4;
    break;
  case 0x36:
    q = zpx(m);
    {
      uint8_t v = rd(m, q), o = m->p & C;
      m->p = (m->p & ~C) | (v >> 7);
      v = (v << 1) | o;
      wr(m, q, v);
      nz(m, v);
    }
    c = 6;
    break;
  case 0x38:
    m->p |= C;
    break;
  case 0x39:
    m->a &= rd(m, ay(m));
    nz(m, m->a);
    c = 4;
    break;
  case 0x3d:
    m->a &= rd(m, ax(m));
    nz(m, m->a);
    c = 4;
    break;
  case 0x3e:
    q = ax(m);
    {
      uint8_t v = rd(m, q), o = m->p & C;
      m->p = (m->p & ~C) | (v >> 7);
      v = (v << 1) | o;
      wr(m, q, v);
      nz(m, v);
    }
    c = 7;
    break;
  case 0x40:
    m->p = (pull(m) & ~B) | U;
    m->pc = pull(m);
    m->pc |= (uint16_t)pull(m) << 8;
    c = 6;
    break;
  case 0x41:
    m->a ^= rd(m, ix(m));
    nz(m, m->a);
    c = 6;
    break;
  case 0x45:
    m->a ^= rd(m, zp(m));
    nz(m, m->a);
    c = 3;
    break;
  case 0x46:
    q = zp(m);
    {
      uint8_t v = rd(m, q);
      m->p = (m->p & ~C) | (v & 1);
      v >>= 1;
      wr(m, q, v);
      nz(m, v);
    }
    c = 5;
    break;
  case 0x48:
    push(m, m->a);
    c = 3;
    break;
  case 0x49:
    m->a ^= fetch(m);
    nz(m, m->a);
    break;
  case 0x4a:
    m->p = (m->p & ~C) | (m->a & 1);
    m->a >>= 1;
    nz(m, m->a);
    break;
  case 0x4c:
    m->pc = word(m);
    c = 3;
    break;
  case 0x4d:
    m->a ^= rd(m, word(m));
    nz(m, m->a);
    c = 4;
    break;
  case 0x4e:
    q = word(m);
    {
      uint8_t v = rd(m, q);
      m->p = (m->p & ~C) | (v & 1);
      v >>= 1;
      wr(m, q, v);
      nz(m, v);
    }
    c = 6;
    break;
  case 0x50:
    BRANCH(!(m->p & V));
    break;
  case 0x51:
    m->a ^= rd(m, iy(m));
    nz(m, m->a);
    c = 5;
    break;
  case 0x55:
    m->a ^= rd(m, zpx(m));
    nz(m, m->a);
    c = 4;
    break;
  case 0x56:
    q = zpx(m);
    {
      uint8_t v = rd(m, q);
      m->p = (m->p & ~C) | (v & 1);
      v >>= 1;
      wr(m, q, v);
      nz(m, v);
    }
    c = 6;
    break;
  case 0x58:
    m->p &= ~I;
    break;
  case 0x59:
    m->a ^= rd(m, ay(m));
    nz(m, m->a);
    c = 4;
    break;
  case 0x5d:
    m->a ^= rd(m, ax(m));
    nz(m, m->a);
    c = 4;
    break;
  case 0x5e:
    q = ax(m);
    {
      uint8_t v = rd(m, q);
      m->p = (m->p & ~C) | (v & 1);
      v >>= 1;
      wr(m, q, v);
      nz(m, v);
    }
    c = 7;
    break;
  case 0x60:
    m->pc = pull(m);
    m->pc |= (uint16_t)pull(m) << 8;
    m->pc++;
    c = 6;
    break;
  case 0x61:
    adc(m, rd(m, ix(m)));
    c = 6;
    break;
  case 0x65:
    adc(m, rd(m, zp(m)));
    c = 3;
    break;
  case 0x66:
    q = zp(m);
    {
      uint8_t v = rd(m, q), o = (m->p & C) << 7;
      m->p = (m->p & ~C) | (v & 1);
      v = (v >> 1) | o;
      wr(m, q, v);
      nz(m, v);
    }
    c = 5;
    break;
  case 0x68:
    m->a = pull(m);
    nz(m, m->a);
    c = 4;
    break;
  case 0x69:
    adc(m, fetch(m));
    break;
  case 0x6a: {
    uint8_t o = (m->p & C) << 7;
    m->p = (m->p & ~C) | (m->a & 1);
    m->a = (m->a >> 1) | o;
    nz(m, m->a);
  } break;
  case 0x6c: {
    uint16_t p = word(m);
    m->pc = (uint16_t)(rd(m, p) |
                       rd(m, (uint16_t)((p & 0xff00) | ((p + 1) & 255))) << 8);
  }
    c = 5;
    break;
  case 0x6d:
    adc(m, rd(m, word(m)));
    c = 4;
    break;
  case 0x70:
    BRANCH(m->p & V);
    break;
  case 0x71:
    adc(m, rd(m, iy(m)));
    c = 5;
    break;
  case 0x6e:
    q = word(m);
    {
      uint8_t v = rd(m, q), o = (m->p & C) << 7;
      m->p = (m->p & ~C) | (v & 1);
      v = (v >> 1) | o;
      wr(m, q, v);
      nz(m, v);
    }
    c = 6;
    break;
  case 0x75:
    adc(m, rd(m, zpx(m)));
    c = 4;
    break;
  case 0x78:
    m->p |= I;
    break;
  case 0x79:
    adc(m, rd(m, ay(m)));
    c = 4;
    break;
  case 0x7d:
    adc(m, rd(m, ax(m)));
    c = 4;
    break;
  case 0x76:
    q = zpx(m);
    {
      uint8_t v = rd(m, q), o = (m->p & C) << 7;
      m->p = (m->p & ~C) | (v & 1);
      v = (v >> 1) | o;
      wr(m, q, v);
      nz(m, v);
    }
    c = 6;
    break;
  case 0x7e:
    q = ax(m);
    {
      uint8_t v = rd(m, q), o = (m->p & C) << 7;
      m->p = (m->p & ~C) | (v & 1);
      v = (v >> 1) | o;
      wr(m, q, v);
      nz(m, v);
    }
    c = 7;
    break;
  case 0x81:
    STORE(m->a, ix(m));
    c = 6;
    break;
  case 0x84:
    STORE(m->y, zp(m));
    c = 3;
    break;
  case 0x85:
    STORE(m->a, zp(m));
    c = 3;
    break;
  case 0x86:
    STORE(m->x, zp(m));
    c = 3;
    break;
  case 0x88:
    m->y--;
    nz(m, m->y);
    break;
  case 0x8a:
    m->a = m->x;
    nz(m, m->a);
    break;
  case 0x8c:
    STORE(m->y, word(m));
    c = 4;
    break;
  case 0x8d:
    STORE(m->a, word(m));
    c = 4;
    break;
  case 0x8e:
    STORE(m->x, word(m));
    c = 4;
    break;
  case 0x90:
    BRANCH(!(m->p & C));
    break;
  case 0x91:
    STORE(m->a, iy(m));
    c = 6;
    break;
  case 0x94:
    STORE(m->y, zpx(m));
    c = 4;
    break;
  case 0x95:
    STORE(m->a, zpx(m));
    c = 4;
    break;
  case 0x96:
    STORE(m->x, zpy(m));
    c = 4;
    break;
  case 0x98:
    m->a = m->y;
    nz(m, m->a);
    break;
  case 0x99:
    STORE(m->a, ay(m));
    c = 5;
    break;
  case 0x9a:
    m->s = m->x;
    break;
  case 0x9d:
    STORE(m->a, ax(m));
    c = 5;
    break;
  case 0xa0:
    LOAD(m->y, m->pc++);
    break;
  case 0xa1:
    LOAD(m->a, ix(m));
    c = 6;
    break;
  case 0xa2:
    LOAD(m->x, m->pc++);
    break;
  case 0xa4:
    LOAD(m->y, zp(m));
    c = 3;
    break;
  case 0xa5:
    LOAD(m->a, zp(m));
    c = 3;
    break;
  case 0xa6:
    LOAD(m->x, zp(m));
    c = 3;
    break;
  case 0xa8:
    m->y = m->a;
    nz(m, m->y);
    break;
  case 0xa9:
    LOAD(m->a, m->pc++);
    break;
  case 0xaa:
    m->x = m->a;
    nz(m, m->x);
    break;
  case 0xac:
    LOAD(m->y, word(m));
    c = 4;
    break;
  case 0xad:
    LOAD(m->a, word(m));
    c = 4;
    break;
  case 0xae:
    LOAD(m->x, word(m));
    c = 4;
    break;
  case 0xb0:
    BRANCH(m->p & C);
    break;
  case 0xb1:
    LOAD(m->a, iy(m));
    c = 5;
    break;
  case 0xb4:
    LOAD(m->y, zpx(m));
    c = 4;
    break;
  case 0xb5:
    LOAD(m->a, zpx(m));
    c = 4;
    break;
  case 0xb6:
    LOAD(m->x, zpy(m));
    c = 4;
    break;
  case 0xb8:
    m->p &= ~V;
    break;
  case 0xb9:
    LOAD(m->a, ay(m));
    c = 4;
    break;
  case 0xba:
    m->x = m->s;
    nz(m, m->x);
    break;
  case 0xbc:
    LOAD(m->y, ax(m));
    c = 4;
    break;
  case 0xbd:
    LOAD(m->a, ax(m));
    c = 4;
    break;
  case 0xbe:
    LOAD(m->x, ay(m));
    c = 4;
    break;
  case 0xc0:
    cmp(m, m->y, fetch(m));
    break;
  case 0xc1:
    cmp(m, m->a, rd(m, ix(m)));
    c = 6;
    break;
  case 0xc4:
    cmp(m, m->y, rd(m, zp(m)));
    c = 3;
    break;
  case 0xc5:
    cmp(m, m->a, rd(m, zp(m)));
    c = 3;
    break;
  case 0xc6:
    q = zp(m);
    {
      uint8_t v = rd(m, q) - 1;
      wr(m, q, v);
      nz(m, v);
    }
    c = 5;
    break;
  case 0xc8:
    m->y++;
    nz(m, m->y);
    break;
  case 0xc9:
    cmp(m, m->a, fetch(m));
    break;
  case 0xca:
    m->x--;
    nz(m, m->x);
    break;
  case 0xcc:
    cmp(m, m->y, rd(m, word(m)));
    c = 4;
    break;
  case 0xcd:
    cmp(m, m->a, rd(m, word(m)));
    c = 4;
    break;
  case 0xce:
    q = word(m);
    {
      uint8_t v = rd(m, q) - 1;
      wr(m, q, v);
      nz(m, v);
    }
    c = 6;
    break;
  case 0xd0:
    BRANCH(!(m->p & Z));
    break;
  case 0xd1:
    cmp(m, m->a, rd(m, iy(m)));
    c = 5;
    break;
  case 0xd5:
    cmp(m, m->a, rd(m, zpx(m)));
    c = 4;
    break;
  case 0xd8:
    m->p &= ~D;
    break;
  case 0xd6:
    q = zpx(m);
    {
      uint8_t v = rd(m, q) - 1;
      wr(m, q, v);
      nz(m, v);
    }
    c = 6;
    break;
  case 0xd9:
    cmp(m, m->a, rd(m, ay(m)));
    c = 4;
    break;
  case 0xdd:
    cmp(m, m->a, rd(m, ax(m)));
    c = 4;
    break;
  case 0xde:
    q = ax(m);
    {
      uint8_t v = rd(m, q) - 1;
      wr(m, q, v);
      nz(m, v);
    }
    c = 7;
    break;
  case 0xe0:
    cmp(m, m->x, fetch(m));
    break;
  case 0xe1:
    sbc(m, rd(m, ix(m)));
    c = 6;
    break;
  case 0xe4:
    cmp(m, m->x, rd(m, zp(m)));
    c = 3;
    break;
  case 0xe5:
    sbc(m, rd(m, zp(m)));
    c = 3;
    break;
  case 0xe6:
    q = zp(m);
    {
      uint8_t v = rd(m, q) + 1;
      wr(m, q, v);
      nz(m, v);
    }
    c = 5;
    break;
  case 0xe8:
    m->x++;
    nz(m, m->x);
    break;
  case 0xe9:
  case 0xeb:
    sbc(m, fetch(m));
    break;
  case 0xea:
    break;
  case 0xec:
    cmp(m, m->x, rd(m, word(m)));
    c = 4;
    break;
  case 0xed:
    sbc(m, rd(m, word(m)));
    c = 4;
    break;
  case 0xee:
    q = word(m);
    {
      uint8_t v = rd(m, q) + 1;
      wr(m, q, v);
      nz(m, v);
    }
    c = 6;
    break;
  case 0xf0:
    BRANCH(m->p & Z);
    break;
  case 0xf1:
    sbc(m, rd(m, iy(m)));
    c = 5;
    break;
  case 0xf5:
    sbc(m, rd(m, zpx(m)));
    c = 4;
    break;
  case 0xf8:
    m->p |= D;
    break;
  case 0xf6:
    q = zpx(m);
    {
      uint8_t v = rd(m, q) + 1;
      wr(m, q, v);
      nz(m, v);
    }
    c = 6;
    break;
  case 0xf9:
    sbc(m, rd(m, ay(m)));
    c = 4;
    break;
  case 0xfd:
    sbc(m, rd(m, ax(m)));
    c = 4;
    break;
  case 0xfe:
    q = ax(m);
    {
      uint8_t v = rd(m, q) + 1;
      wr(m, q, v);
      nz(m, v);
    }
    c = 7;
    break;
  default: /* stable two-byte NOP forms */
    if ((op & 0x1f) == 0x04 || (op & 0x1f) == 0x14)
      fetch(m);
    break;
  }
  m->cycles += (unsigned)c;
  return c;
}
