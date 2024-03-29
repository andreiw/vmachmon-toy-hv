#define LOG_PFX MON
#include "mon.h"
#include "socket.h"
#include "guest.h"
#include "pmem.h"
#include "vmm.h"
#include "rom.h"

#define PICOL_IMPLEMENTATION
#define PICOL_INT_BASE_16    1
#define fflush(x)
#define fprintf(x, fmt, ...) mon_fprintf(x, fmt, ## __VA_ARGS__)
#include "picol.h"

#define SOURCE_FILE "pvp.pcl"
#define PORT        7001
#define IBUF_SIZE   PAGE_SIZE

static socket_t s;
static char *ibuf;
static unsigned ibuf_index;
static picolInterp *interp;
static err_t picol_err;
static bool activated;
static bool pend_prompt;

PICOL_COMMAND(quit) {
  PICOL_ARITY2(argc == 1, "quit");

  *(err_t *)pd = ERR_SHUTDOWN;
  return PICOL_OK;
}

PICOL_COMMAND(cont) {
  PICOL_ARITY2(argc == 1, "cont");

  *(err_t *)pd = ERR_CONTINUE;
  return PICOL_OK;
}

PICOL_COMMAND(pause) {
  PICOL_ARITY2(argc == 1, "pause");

  *(err_t *)pd = ERR_PAUSE;
  return PICOL_OK;
}

PICOL_COMMAND(ss) {
  PICOL_ARITY2(argc == 1, "ss");

  mon_printf("Single-stepping is %s\n",
             guest_toggle_ss() ? "on" : "off");
  return PICOL_OK;
}

PICOL_COMMAND(reg) {
  PICOL_ARITY(argc == 1 || argc == 2);
  int v;
  uint32_t *r = NULL;

  if (PICOL_EQ(argv[0], "ctr")) {
    r = (uint32_t *) &guest->regs->ppcCTR;
  } else if (PICOL_EQ(argv[0], "xer")) {
    r = (uint32_t *) &guest->regs->ppcXER;
  } else if (PICOL_EQ(argv[0], "cr")) {
    r = (uint32_t *) &guest->regs->ppcCR;
  } else if (PICOL_EQ(argv[0], "ctr")) {
    r = (uint32_t *) &guest->regs->ppcCTR;
  } else if (PICOL_EQ(argv[0], "fpscr")) {
    r = (uint32_t *) &guest->vmm->vmm_proc_state.ppcFPSCR;
  } else if (PICOL_EQ(argv[0], "pc")) {
    r = (uint32_t *) &guest->regs->ppcPC;
  } else if (PICOL_EQ(argv[0], "lr")) {
    r = (uint32_t *) &guest->regs->ppcLR;
  } else if (PICOL_EQ(argv[0], "msr")) {
    r = (uint32_t *) &guest->msr;
  } else if (PICOL_EQ(argv[0], "sdr1")) {
    r = (uint32_t *) &guest->sdr1;
  } else if (argv[0][0] == 'r') {
    PICOL_SCAN_INT(v, argv[0] + 1);
    r = (uint32_t *) &guest->regs->ppcGPRs[v];
  }

  if (r != NULL) {
    if (argc == 1) {
      picolSetIntResult(interp, *r);
    } else {
      PICOL_SCAN_INT(v, argv[1]);
      *r = v;
    }

    return PICOL_OK;
  }

  return PICOL_ERR;
}

PICOL_COMMAND(gra) {
  PICOL_ARITY2(argc == 2, "gra ea");

  gea_t ea;
  gra_t ra;

  PICOL_SCAN_INT(ea, argv[1]);
  err_t err = guest_backmap(ea, &ra);
  if (err == ERR_NONE) {
    picolSetIntResult(interp, ra);
    return PICOL_OK;
  }

  return picolErrFmt(interp, "%s", err_to_string(err));
}

PICOL_COMMAND(cpu) {
  PICOL_ARITY(argc == 1);

  guest_mon_dump();

  return PICOL_OK;
}

PICOL_COMMAND(rom) {
  PICOL_ARITY(argc == 1);

  rom_mon_dump();

  return PICOL_OK;
}

PICOL_COMMAND(dump) {
  PICOL_ARITY2(argc == 3 || argc == 2, "d8/d16/d32 ea ?count");

  gea_t ea;
  length_t stride;
  count_t done, each;
  err_t err = ERR_NONE;
  char t = argv[0][1];
  count_t count = 16;

  PICOL_SCAN_INT(ea, argv[1]);
  if (argc == 3) {
    PICOL_SCAN_INT(count, argv[2]);
  }
  done = 0;

  if (count == 0) {
    return PICOL_OK;
  }

  if (t == '8' || t == 'c') {
    each = 16;
    stride = 1;
  } else if (t == '1') {
    each = 8;
    stride = 2;
  } else {
    each = 4;
    stride = 4;
  }

  while (count--) {
    if (done != 0 && (done % each) == 0) {
      mon_printf("\n");
    }

    if (done % each == 0) {
      mon_printf("0x%x: ", ea);
    }

    if (t == '8' || t == 'c') {
      uint8_t v8;
      err = guest_from_x(&v8, ea);
      if (err != ERR_NONE) {
        break;
      }

      if (t == '8') {
        mon_printf("0x%02x ", v8);
      } else {
        mon_printf("'%c' ", isprint(v8) ? v8 : '.');
      }
    } else if(t == '1') {
      uint16_t v16;
      err = guest_from_x(&v16, ea);
      if (err != ERR_NONE) {
        break;
      }

      mon_printf("0x%04x ", v16);
    } else {
      uint32_t v;
      err = guest_from_x(&v, ea);
      if (err != ERR_NONE) {
        break;
      }

      mon_printf("0x%08x ", v);
    }

    ea += stride;
    done++;
  }

  mon_printf("\n");

  if (err == ERR_NONE) {
    picolSetResult(interp, "");
    return PICOL_OK;
  }

  return picolErrFmt(interp, "%s", err_to_string(err));
}

PICOL_COMMAND(memread) {
  PICOL_ARITY2(argc == 2 || argc == 3, "prc/pr8/pr16/pr32/mrc/mr8/mr16/mr32 addr ?count");

  gea_t ea;
  count_t count = 1;
  err_t err = ERR_NONE;
  char buf[PICOL_MAX_STR] = "";
  char formatted[sizeof("0xyyyyxxxx")];
  char t = argv[0][2];
  bool virt = argv[0][0] == 'm';

  PICOL_SCAN_INT(ea, argv[1]);
  if (argc == 3) {
    PICOL_SCAN_INT(count, argv[2]);
  }

  while (count--) {
    if (t == '8' || t == 'c') {
      uint8_t v8;
      err = virt ? guest_from_x(&v8, ea) :
        pmem_from_x(&v8, ea);
      if (err != ERR_NONE) {
        break;
      }

      ea += 1;
      PICOL_SNPRINTF(formatted, sizeof(formatted),
                     t == 'c' ? "%c" : "0x%02x", v8);
    } else if(t == '1') {
      uint16_t v16;
      err = virt ? guest_from_x(&v16, ea) :
        pmem_from_x(&v16, ea);
      if (err != ERR_NONE) {
        break;
      }

      ea += 2;
      PICOL_SNPRINTF(formatted, sizeof(formatted), "0x%04x", v16);
    } else {
      uint32_t v;
      err = virt ? guest_from_x(&v, ea) : pmem_from_x(&v, ea);
      if (err != ERR_NONE) {
        break;
      }

      ea += 4;
      PICOL_SNPRINTF(formatted, sizeof(formatted), "0x%08x", v);
    }

    PICOL_LAPPEND(buf, formatted);
  }

  if (err == ERR_NONE) {
    return picolSetResult(interp, buf);
  }

  return picolErrFmt(interp, "%s", err_to_string(err));
}

PICOL_COMMAND(memreadstring) {
  PICOL_ARITY2(argc == 2, "mrs ea");

  gea_t ea;
  err_t err = ERR_NONE;
  char buf[PICOL_MAX_STR] = "";

  PICOL_SCAN_INT(ea, argv[1]);

  while (1) {
    char c;
    err = guest_from_x(&c, ea);
    if (err != ERR_NONE) {
      break;
    }

    ea += 1;
    if (c == '\0') {
      break;
    }
    strncat(buf, &c, 1);
  }

  if (err == ERR_NONE) {
    return picolSetResult(interp, buf);
  }

  return picolErrFmt(interp, "%s", err_to_string(err));
}

int
mon_fprintf(void *unused,
            const char *fmt,
            ...)
{
  int ret;
  static char buf[PICOL_MAX_STR];
  va_list ap;

  va_start(ap, fmt);
  ret = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (ret > 0) {
    socket_out(&s, buf, ret);
  }

  return ret;
}

static void
mon_prompt(void)
{
  mon_printf("(%s) ", activated ? "sync" : "async");
  ibuf_index = 0;
}

static void
mon_on_connect(socket_t *s)
{
  const char *banner =
    "\nThis is the PVP monitor console\r\n"
    "-------------------------------\r\n\n";

  mon_printf("%s", banner);
  pend_prompt = true;
}

static void
mon_on_disconnect(socket_t *s)
{
  const char *banner =
    "\r\n\nMonitor console closing...\r\n";

  socket_out(s, banner, strlen(banner));
}

err_t
mon_init(void)
{
  int rc;
  err_t err;

  ibuf = malloc(IBUF_SIZE);
  if (ibuf == NULL) {
    POSIX_ERROR(errno, "ibuf");
    return ERR_POSIX;
  }

  interp = picolCreateInterp();
  picolRegisterCmd(interp, "quit", picol_quit, &picol_err);
  picolRegisterCmd(interp, "cont", picol_cont, &picol_err);
  picolRegisterCmd(interp, "pause", picol_pause, &picol_err);
  picolRegisterCmd(interp, "ss", picol_ss, NULL);
  picolRegisterCmd(interp, "r0", picol_reg, NULL);
  picolRegisterCmd(interp, "r1", picol_reg, NULL);
  picolRegisterCmd(interp, "r2", picol_reg, NULL);
  picolRegisterCmd(interp, "r3", picol_reg, NULL);
  picolRegisterCmd(interp, "r4", picol_reg, NULL);
  picolRegisterCmd(interp, "r5", picol_reg, NULL);
  picolRegisterCmd(interp, "r6", picol_reg, NULL);
  picolRegisterCmd(interp, "r7", picol_reg, NULL);
  picolRegisterCmd(interp, "r8", picol_reg, NULL);
  picolRegisterCmd(interp, "r9", picol_reg, NULL);
  picolRegisterCmd(interp, "r10", picol_reg, NULL);
  picolRegisterCmd(interp, "r11", picol_reg, NULL);
  picolRegisterCmd(interp, "r12", picol_reg, NULL);
  picolRegisterCmd(interp, "r13", picol_reg, NULL);
  picolRegisterCmd(interp, "r14", picol_reg, NULL);
  picolRegisterCmd(interp, "r15", picol_reg, NULL);
  picolRegisterCmd(interp, "r16", picol_reg, NULL);
  picolRegisterCmd(interp, "r17", picol_reg, NULL);
  picolRegisterCmd(interp, "r18", picol_reg, NULL);
  picolRegisterCmd(interp, "r19", picol_reg, NULL);
  picolRegisterCmd(interp, "r20", picol_reg, NULL);
  picolRegisterCmd(interp, "r21", picol_reg, NULL);
  picolRegisterCmd(interp, "r22", picol_reg, NULL);
  picolRegisterCmd(interp, "r23", picol_reg, NULL);
  picolRegisterCmd(interp, "r24", picol_reg, NULL);
  picolRegisterCmd(interp, "r25", picol_reg, NULL);
  picolRegisterCmd(interp, "r26", picol_reg, NULL);
  picolRegisterCmd(interp, "r27", picol_reg, NULL);
  picolRegisterCmd(interp, "r28", picol_reg, NULL);
  picolRegisterCmd(interp, "r29", picol_reg, NULL);
  picolRegisterCmd(interp, "r30", picol_reg, NULL);
  picolRegisterCmd(interp, "r31", picol_reg, NULL);
  picolRegisterCmd(interp, "ctr", picol_reg, NULL);
  picolRegisterCmd(interp, "xer", picol_reg, NULL);
  picolRegisterCmd(interp, "cr", picol_reg, NULL);
  picolRegisterCmd(interp, "pc", picol_reg, NULL);
  picolRegisterCmd(interp, "lr", picol_reg, NULL);
  picolRegisterCmd(interp, "msr", picol_reg, NULL);
  picolRegisterCmd(interp, "sdr1", picol_reg, NULL);
  picolRegisterCmd(interp, "gra", picol_gra, NULL);
  picolRegisterCmd(interp, "mr8", picol_memread, NULL);
  picolRegisterCmd(interp, "mr16", picol_memread, NULL);
  picolRegisterCmd(interp, "mr32", picol_memread, NULL);
  picolRegisterCmd(interp, "mrc", picol_memread, NULL);
  picolRegisterCmd(interp, "pr8", picol_memread, NULL);
  picolRegisterCmd(interp, "pr16", picol_memread, NULL);
  picolRegisterCmd(interp, "pr32", picol_memread, NULL);
  picolRegisterCmd(interp, "prc", picol_memread, NULL);
  picolRegisterCmd(interp, "mrs", picol_memreadstring, NULL);
  picolRegisterCmd(interp, "d8", picol_dump, NULL);
  picolRegisterCmd(interp, "dc", picol_dump, NULL);
  picolRegisterCmd(interp, "d16", picol_dump, NULL);
  picolRegisterCmd(interp, "d32", picol_dump, NULL);
  picolRegisterCmd(interp, "cpu", picol_cpu, NULL);
  picolRegisterCmd(interp, "rom", picol_rom, NULL);

  rc = picolSource(interp, SOURCE_FILE);
  if (rc != PICOL_OK) {
    picolVar *v = picolGetVar(interp, "::errorInfo");
    if (v != NULL) {
      LOG("Sourcing '%s' failed: %s", SOURCE_FILE, v->val);
    }
  }

  s.port = PORT;
  s.on_connect = mon_on_connect;
  s.on_disconnect = mon_on_disconnect;
  err = socket_init(&s);
  ON_ERROR("socket", err, done);

 done:
  return err;
}

err_t
mon_activate(void)
{
  err_t err;
  bool was_connected = socket_connected(&s);

  activated = true;
  if (!was_connected) {
    LOG("Waiting for monitor console connection on %u", PORT);
  }
  while (socket_handle_connect(&s) != ERR_NONE);

  if (!pend_prompt) {
    mon_printf("\n");
    pend_prompt = true;
  }

  if (!was_connected) {
    LOG("Monitor console connected");
  }

  while (1) {
    err = mon_check();

    if (err == ERR_CONTINUE) {
      err = ERR_NONE;
      break;
    } else if (err != ERR_NONE) {
      break;
    }
  }

  activated = false;
  return err;
}

void
mon_bye(void)
{
  socket_disconnect(&s);
}

err_t
mon_trace(void)
{
  int rc;

  picolVar *v = picolGetVar(interp, "on_trace");
  if (v == NULL) {
    return ERR_NONE;
  }

  rc = picolEval(interp, v->val);
  if (interp->result[0] != '\0' || rc != PICOL_OK) {
    mon_printf("[%d] %s\n", rc, interp->result);
  }

  if (picol_err != ERR_NONE) {
    err_t r = picol_err;
    picol_err = ERR_NONE;

    if (r != ERR_CONTINUE) {
      return r;
    }
  }

  return ERR_NONE;
}

err_t
mon_check(void)
{
  char c;

  if (pend_prompt) {
    mon_prompt();
    pend_prompt = false;
  }

  if (socket_in(&s, &c, 1) == 0) {
    return ERR_NONE;
  }

  if (c == '\b') {
    if (ibuf_index != 0) {
      ibuf_index--;
    }
  } else if (c == '\n') {
    int rc;

    ibuf[ibuf_index] = '\0';
    rc = picolEval(interp, ibuf);
    if (interp->result[0] != '\0' || rc != PICOL_OK) {
      mon_printf("[%d] %s\n", rc, interp->result);
    }
    pend_prompt = true;

    if (picol_err != ERR_NONE) {
      err_t r = picol_err;
      picol_err = ERR_NONE;
      return r;
    }
    return ERR_NONE;
  } else if (c == '\r') {
    return ERR_NONE;
  }

  if (ibuf_index == (IBUF_SIZE - 1)) {
    return ERR_NONE;
  }

  ibuf[ibuf_index] = c;
  ibuf_index++;  

  return ERR_NONE;
}
