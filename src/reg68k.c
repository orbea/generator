/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */

#include "generator.h"
#include "registers.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>

#include "reg68k.h"
#include "mem68k.h"
#include "cpu68k.h"
#include "cpuz80.h"
#include "vdp.h"
#include "ui.h"
#include "compile.h"
#include "gensound.h"

/*** externed variables ***/

#if (!(defined(PROCESSOR_ARM) || defined(PROCESSOR_SPARC) \
       || defined(PROCESSOR_INTEL)))
  uint32 reg68k_pc;
  uint32 *reg68k_regs;
  t_sr reg68k_sr;
#endif

/*** forward references ***/


/*** reg68k_external_step - execute one instruction ***/

unsigned int reg68k_external_step(void)
{
  static t_ipc ipc;
  static t_iib *piib;
  jmp_buf jb;
  static unsigned int clks;

  if (!(piib = cpu68k_iibtable[fetchword(regs.pc)]))
    ui_err("Invalid instruction @ %08X\n", regs.pc);

  /* !!! entering global register usage area !!! */

  if (!setjmp(jb)) {
    /* move PC and register block into global processor register variables */
    reg68k_pc = regs.pc;
    reg68k_regs = regs.regs;
    reg68k_sr = regs.sr;

    if (regs.pending && ((reg68k_sr.sr_int >> 8) & 7) < regs.pending)
      reg68k_internal_autovector(regs.pending);

    cpu68k_ipc(reg68k_pc,
	       mem68k_memptr[(reg68k_pc>>12) & 0xfff](reg68k_pc & 0xFFFFFF),
	       piib, &ipc);
    cpu68k_functable[fetchword(reg68k_pc)*2+1](&ipc);
    cpu68k_clocks+= piib->clocks;
    clks = cpu68k_clocks;
    /* restore global registers back to permanent storage */
    regs.pc = reg68k_pc;
    regs.sr = reg68k_sr;
    longjmp(jb, 1);
  }
  return clks; /* number of clocks done */
}

/*** reg68k_external_execute - execute at least given number of clocks,
     and return number of clocks executed too much (-ve) ***/

unsigned int reg68k_external_execute(unsigned int clocks)
{
  unsigned int index, i;
  t_ipclist *list;
  t_ipc *ipc;
  uint32 pc24;
  jmp_buf jb;
  static t_ipc step_ipc;
  static t_iib *step_piib;
  static int clks;

  clks = clocks;

  if (!setjmp(jb)) {
    /* move PC and register block into global variables */
    reg68k_pc = regs.pc;
    reg68k_regs = regs.regs;
    reg68k_sr = regs.sr;

    if (regs.pending && ((reg68k_sr.sr_int >> 8) & 7) < regs.pending)
      reg68k_internal_autovector(regs.pending);

    do {
      pc24 = reg68k_pc & 0xffffff;
      if ((pc24 & 0xff0000) == 0xff0000) {
	/* executing code from RAM, do not use compiled information */
	do {
	  step_piib = cpu68k_iibtable[fetchword(reg68k_pc)];
	  cpu68k_ipc(reg68k_pc,
		     mem68k_memptr[(reg68k_pc>>12) &
				  0xfff](reg68k_pc & 0xFFFFFF),
		     step_piib, &step_ipc);
	  cpu68k_functable[fetchword(reg68k_pc)*2+1](&step_ipc);
	  clks-= step_piib->clocks;
	  cpu68k_clocks+= step_piib->clocks;
	} while (!step_piib->flags.endblk);
	list = NULL; /* stop compiler warning ;(  */
      } else {
	index = (pc24>>1) & (LEN_IPCLISTTABLE-1);
	list = ipclist[index];
	while(list && (list->pc != pc24)) {
	  list = list->next;
	}
#ifdef PROCESSOR_ARM
	if (!list) {
	  list = cpu68k_makeipclist(pc24);
	  list->next = ipclist[index];
	  ipclist[index] = list;
	  list->compiled = compile_make(list);
	}
	list->compiled((t_ipc *)(list+1));
#else
	if (!list) {
	  /* LOG_USER(("Making IPC list @ %08x", pc24)); */
	  list = cpu68k_makeipclist(pc24);
	  list->next = ipclist[index];
	  ipclist[index] = list;
	}
	ipc = (t_ipc *)(list+1);
	do {
	  ipc->function(ipc);
	  ipc++;
	} while (*(int *)ipc);
#endif
	clks-= list->clocks;
	cpu68k_clocks+= list->clocks;
      }
    } while (clks > 0);
    /* restore global registers back to permanent storage */
    regs.pc = reg68k_pc;
    regs.sr = reg68k_sr;
    longjmp(jb, 1);
  }
  return -clks; /* i.e. number of clocks done too much */
}

/*** reg68k_external_autovector - for external use ***/

void reg68k_external_autovector(int avno)
{
  jmp_buf jb;

  if (!setjmp(jb)) {
    /* move PC and register block into global processor register variables */
    reg68k_pc = regs.pc;
    reg68k_regs = regs.regs;
    reg68k_sr = regs.sr;

    reg68k_internal_autovector(avno);

    /* restore global registers back to permanent storage */
    regs.pc = reg68k_pc;
    regs.sr = reg68k_sr;
    longjmp(jb, 1);
  }
}

/*** reg68k_internal_autovector - go to autovector - this call assumes global
     registers are already setup ***/

void reg68k_internal_autovector(int avno)
{
  int curlevel = (reg68k_sr.sr_int>>8) & 7;
  uint32 tmpaddr;

  LOG_DEBUG1(("autovector %d", avno));
  if (curlevel < avno || avno == 7) {
    if (regs.stop) {
      LOG_DEBUG1(("stop finished"));
      /* autovector whilst in a STOP instruction */
      reg68k_pc+= 4;
      regs.stop = 0;
    }
    if (!reg68k_sr.sr_struct.s) {
      regs.regs[15]^= regs.sp;   /* swap A7 and SP */
      regs.sp^= regs.regs[15];
      regs.regs[15]^= regs.sp;
      reg68k_sr.sr_struct.s = 1;
    }
    regs.regs[15]-=4;
    storelong(regs.regs[15], reg68k_pc);
    regs.regs[15]-=2;
    storeword(regs.regs[15], reg68k_sr.sr_int);
    reg68k_sr.sr_struct.t = 0;
    reg68k_sr.sr_int&= ~0x0700;
    reg68k_sr.sr_int|= avno << 8;
    tmpaddr = reg68k_pc;
    reg68k_pc = fetchlong((V_AUTO+avno-1)*4);
    LOG_DEBUG1(("AUTOVECTOR %d: %X -> %X", avno, tmpaddr, reg68k_pc));
    regs.pending = 0;
  } else {
    LOG_DEBUG1(("%08X autovector %d pending", reg68k_pc, avno));
    regs.pending = avno;
  }
}

/*** reg68k_internal_vector - go to vector - this call assumes global
     registers are already setup ***/

void reg68k_internal_vector(int vno, uint32 oldpc)
{
  LOG_DEBUG1(("Vector %d called from %8x!", vno, regs.pc));
  if (!reg68k_sr.sr_struct.s) {
    regs.regs[15]^= regs.sp;   /* swap A7 and SP */
    regs.sp^= regs.regs[15];
    regs.regs[15]^= regs.sp;
    reg68k_sr.sr_struct.s = 1;
  }
  regs.regs[15]-=4;
  storelong(regs.regs[15], oldpc);
  regs.regs[15]-=2;
  storeword(regs.regs[15], reg68k_sr.sr_int);
  reg68k_pc = fetchlong(vno*4);
  LOG_DEBUG1(("VECTOR %d: %X -> %X\n", vno, oldpc, reg68k_pc));
}   
