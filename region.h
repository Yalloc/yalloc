/* region.h - regions

   This file is part of yalloc, yet another memory allocator with emphasis on efficiency and compactness.

   SPDX-FileCopyrightText: © 2024 Joris van der Geer
   SPDX-License-Identifier: GPL-3.0-or-later
*/
#define Dirmsk (Dir - 1)

static void delregmem(heap *hb,region *reg)
{
  ub4 mapcnt = 1;
  size_t ulen = (1ul << reg->order);
  if (reg->user) osunmem(__LINE__,Fregion,hb,reg->user,ulen,"region user");
  if (reg->meta) {
    osunmem(__LINE__,Fregion,hb,reg->meta,reg->metalen,"region meta");
    mapcnt = 2;
  }
  reg->user = nil;
  reg->meta = nil;
  atomic_fetch_sub_explicit(&global_mapcnt,mapcnt,memory_order_relaxed);
}

static struct direntry *newdir(heap *hb)
{
  struct direntry *dp;
  ub4 pos,len;

  pos = hb->dirmem_pos;
  if (pos >= hb->dirmem_len) {
    len = hb->dirmem_len = Dirmem;
    dp = hb->dirmem = osmem(__LINE__,Fregion,hb,len * sizeof (struct direntry),"page dir");
    hb->dirmem_pos = 1; // leave first entry for link
  } else {
    dp = hb->dirmem + pos;
    hb->dirmem_pos = pos + 1;
  }
  return dp;
}

// add reg to directory
static void regdir(heap *hb,region *reg,size_t bas,size_t len)
{
  size_t org,end; // in Minregion steps

  ylog(Fregion,"heap %u reg %s.%u bas %zu len %zu",hb->id,reg ? "nil" : "",reg ? reg->id : 0,bas,len);

  org = bas >> (Dir + Minregion);
  end = (bas + len) >> (Dir + Minregion);

#include "dir.h" // generated by genadm from config.h
}

static bool delregion(heap *hb,region *reg)
{
  region *xreg;
  ub4 frecnt = hb->freeregcnt;
  ub4 allcnt = hb->allocregcnt;
  bool last = (allcnt == frecnt + 1);
  ub4 i;
  size_t ip,len;

  ylog(Fregion,"heap %u delete reg %u",hb->id,reg->id);

  ip = (size_t)reg->user;
  if (reg->typ == Rmmap) {
    delregmem(hb,reg);
    len = reg->len;
  } else len = (1ul << reg->order);
  regdir(hb,nil,ip,len);

  reg->typ = Rnil;
  xreg = hb->freereg;
  if (xreg ) { // chain free regions
    reg->bin = xreg;
  }
  hb->freereg = reg;

  for (i = 0; i < Regfree_trim && reg; i++) {
    if (last) delregmem(hb,reg);
    reg = reg->bin;
  }
  if (reg && i == Regfree_trim) delregmem(hb,reg);
  hb->freeregcnt = frecnt + 1;
  return last;
}

static region *findregion(heap *hb,size_t ip)
{
  size_t x = ip >> Minregion;
  ub2 shift = Maxvm - Dir;
  struct direntry *dp,*dir = hb->rootdir;

  dp = dir + ((x >> shift) &  Dirmsk);
  do {
    if (dp->reg) return dp->reg;
    dir = dp->dir;
    if (!dir) return nil;
    shift -= Dir;
  } while (shift);
  return nil;
}

static region *newregmem(heap *hb)
{
  ub4 pos = hb->regmem_pos;
  region *reg;

  if (pos >= hb->regmem_top) {
    reg = osmem(__LINE__,Fregion,hb,Regmem_inc * sizeof(struct st_region),"region pool");
    if (reg == nil) return nil;
    if (hb->nxtregs) hb->regmem->nxt = reg; // link
    else hb->nxtregs = reg;
    hb->regmem = reg;
    pos = 1;  // leave first entry for link
  } else reg = hb->regmem;
  hb->regmem_top = pos + 1;
  return reg + pos;
}

static region *newregion(heap *hb,void*user,size_t len,size_t admlen,enum Rtype typ)
{
  region *reg;
  size_t adr;
  void *meta;
  ub4 mapcnt = 1;

  if (user == nil) user = osmem(__LINE__,Fregion,hb,len,"mmap region");
  if (user == nil) return nil;
  adr = (size_t)user;

  reg = hb->freereg;
  if (reg ) { // reuse
    if (reg->typ == Rnil) {
      hb->freereg = reg->bin;
    } else hb->freereg = nil;
  } else {
    reg = newregmem(hb);
    if (reg == nil) return nil;
  }
  reg->typ = typ;
  reg->user = user;
  reg->id = hb->allocregcnt++;

  ylog(Fregion,"heap %u new reg %u bas %zx len %zu`b meta %zu`b",hb->id,reg->id,adr,len,admlen);

  switch(typ) {
    case Rnil: break;
    case Rxbuddy: break;
    case Rmmap: break;
    case Rslab:
    case Rbuddy:
      meta = osmem(__LINE__,Fregion,hb,admlen,"region meta");
      if (meta == nil) return nil;
      reg->meta = meta;
      reg->metalen = admlen;
      mapcnt++;
  }
  regdir(hb,reg,adr,len);
  atomic_fetch_add_explicit(&global_mapcnt,mapcnt,memory_order_relaxed);
  return reg;
}
