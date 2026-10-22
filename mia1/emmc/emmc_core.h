/*
 * eMMC (and SD) over an SDHCI controller, by programmed I/O: what the Mi A1's boot shim and its
 * kernel driver (bsd/dev/redwood_tissot_emmc.c, which carries a copy of this file) both use to
 * read the phone's storage. No interrupts, no DMA, one request at a time.
 *
 * The Mi A1's eMMC is on the Snapdragon 625's SDCC1: a standard SDHCI register block ("hc",
 * 0x07824900) beside Qualcomm's own ("core", 0x07824000). The bootloader (LK's aboot) puts the
 * card to sleep and switches the controller out of SDHCI mode before it starts us
 * (target_uninit: mmc_put_card_to_sleep, sdhci_mode_disable), so everything is done again here,
 * in the order LK's platform/msm_shared/sdhci_msm.c and mmc_sdhci.c do it:
 *   - the SDCC1 core clock read as LK left it (192 or 384 MHz), the controller's own divider from it
 *   - SDHCI mode on, a reset, Qualcomm's power-control handshake acknowledged
 *   - the card from sleep to idle (CMD0), then identified and selected at 400 kHz
 *   - 8 data lines, high speed timing, 50 MHz (a card that refuses stays at 1 line, 25 MHz)
 * The same code reads an SD card in a plain SDHCI (msm = 0: QEMU's sdhci-pci, which has no
 * eMMC model, and the Mi A1's microSD slot on SDCC2).
 *
 * The includer defines, before including this file:
 *   uint32_t emmc_rd32(uintptr_t address), void emmc_wr32(uintptr_t address, uint32_t value)
 *   void emmc_udelay(uint32_t microseconds)
 *   void emmc_log(const char *format, ...)       (printf-like: %s %u %x %llx are used)
 *
 * Register layouts: the SD Host Controller Simplified Specification 3.00; JEDEC eMMC 5.0
 * (JESD84-B50); LK (lk2nd) platform/msm_shared/{sdhci,sdhci_msm,mmc_sdhci}.c,
 * platform/msm8953/{msm8953-clock.c,include/platform/iomap.h}; Linux drivers/mmc/host/sdhci-msm.c.
 */
#ifndef EMMC_CORE_H
#define EMMC_CORE_H

/* ---- SDHCI ------------------------------------------------------------------------------------- */

#define SDHCI_ARGUMENT2         0x00
#define SDHCI_BLOCK             0x04            /* [11:0] size, [31:16] count */
#define SDHCI_ARGUMENT          0x08
#define SDHCI_COMMAND           0x0c            /* [15:0] transfer mode, [31:16] command: one write */
#define SDHCI_RESPONSE          0x10            /* 4 words */
#define SDHCI_DATA              0x20
#define SDHCI_PRESENT           0x24
#define SDHCI_HOST_CONTROL      0x28            /* [7:0] host control 1, [15:8] power */
#define SDHCI_CLOCK             0x2c            /* [15:0] clock, [19:16] timeout, [26:24] reset */
#define SDHCI_INT_STATUS        0x30            /* [15:0] normal, [31:16] errors */
#define SDHCI_INT_ENABLE        0x34
#define SDHCI_SIGNAL_ENABLE     0x38
#define SDHCI_CONTROL2          0x3c            /* [31:16] host control 2 */
#define SDHCI_CAPS              0x40
#define SDHCI_VERSION           0xfc            /* [23:16] specification version */

#define PRESENT_CMD_INHIBIT     (1u << 0)
#define PRESENT_DAT_INHIBIT     (1u << 1)
#define PRESENT_CARD            (1u << 16)

#define HOST_4BIT               (1u << 1)
#define HOST_HIGH_SPEED         (1u << 2)
#define HOST_8BIT               (1u << 5)
#define POWER_ON                (1u << 8)
#define POWER_180               (5u << 9)
#define POWER_330               (7u << 9)

#define CLOCK_INTERNAL          (1u << 0)
#define CLOCK_STABLE            (1u << 1)
#define CLOCK_CARD              (1u << 2)
#define TIMEOUT_MAX             (0xeu << 16)
#define RESET_ALL               (1u << 24)
#define RESET_CMD               (1u << 25)
#define RESET_DATA              (1u << 26)

#define INT_COMMAND_DONE        (1u << 0)
#define INT_TRANSFER_DONE       (1u << 1)
#define INT_READ_READY          (1u << 5)
#define INT_WRITE_READY         (1u << 4)
#define INT_ERROR               (1u << 15)
#define INT_ERRORS              0xffff0000u

#define MODE_BLOCK_COUNT        (1u << 1)
#define MODE_AUTO_CMD12         (1u << 2)
#define MODE_READ               (1u << 4)
#define MODE_MULTI              (1u << 5)

#define RESP_NONE               0
#define RESP_136                1
#define RESP_48                 2
#define RESP_48_BUSY            3
#define CMD_CRC                 (1u << 3)
#define CMD_INDEX               (1u << 4)
#define CMD_DATA                (1u << 5)
#define R1                      (RESP_48 | CMD_CRC | CMD_INDEX)
#define R1B                     (RESP_48_BUSY | CMD_CRC | CMD_INDEX)
#define R2                      (RESP_136 | CMD_CRC)
#define R3                      RESP_48

/* ---- Qualcomm's side of SDCC (the "core" registers) and its clocks ------------------------------ */

#define MSM_HC_MODE             0x78            /* [0] SDHCI mode, [13] FF_CLK_SW_RST_DIS */
#define MSM_PWRCTL_STATUS       0xdc            /* [0] bus on, [1] bus off, [2] I/O low, [3] I/O high */
#define MSM_PWRCTL_MASK         0xe0
#define MSM_PWRCTL_CLEAR        0xe4
#define MSM_PWRCTL_CTL          0xe8            /* [0] bus on/off done, [2] I/O switch done */
#define MSM_VERSION             0x50
#define MSM_VENDOR_SPEC         0x10c           /* of "hc" */
#define MSM_VENDOR_CAPS0        0x11c           /* of "hc": capabilities as the controller says them */

#define GCC_SDCC1_CMD_RCGR      0x42004         /* of the global clock controller, 0x01800000 */
#define GCC_SDCC1_CFG_RCGR      0x42008
#define GCC_SDCC1_APPS_CBCR     0x42018
#define GCC_SDCC1_AHB_CBCR      0x4201c
#define RCGR_UPDATE             (1u << 0)
#define CBCR_ENABLE             (1u << 0)
#define CBCR_OFF                (1u << 31)
#define GPLL0_SOURCE            1
#define SDCC_CLOCK_HZ           50000000u       /* GPLL0 (800 MHz) / 16 */

/* ---- the card ---------------------------------------------------------------------------------- */

#define EXT_CSD_PARTITION_CONFIG 179            /* [2:0] which area is accessed: 0 the user area */
#define EXT_CSD_BUS_WIDTH       183
#define EXT_CSD_HS_TIMING       185
#define EXT_CSD_REV             192
#define EXT_CSD_SECTORS         212             /* 4 bytes, little endian */

struct emmc {
	/* filled in by the includer */
	uintptr_t       hc;             /* the SDHCI registers */
	uintptr_t       core;           /* Qualcomm's, 0 for a plain SDHCI */
	uintptr_t       gcc;            /* the global clock controller, 0 to leave the clock as it is */
	const char *    name;
	/* what the card is */
	int             sd;             /* an SD card, not eMMC */
	int             sectors_addressed; /* high capacity: the argument is a sector, not a byte */
	uint32_t        rca;
	uint64_t        sectors;        /* of 512 bytes, in the user area */
	uint32_t        width;          /* data lines */
	uint32_t        hz;
	uint32_t        cid[4];
	/* how it has gone */
	uint64_t        reads, read_sectors, writes, written_sectors, errors, power_acks;
	uint32_t        last_error;     /* SDHCI's error bits of the last failure */
	uint32_t        base_hz;
};

static inline uint32_t
hc_rd(struct emmc *e, uint32_t reg)
{
	return emmc_rd32(e->hc + reg);
}

static inline void
hc_wr(struct emmc *e, uint32_t reg, uint32_t value)
{
	emmc_wr32(e->hc + reg, value);
}

static int
emmc_wait(struct emmc *e, uint32_t reg, uint32_t mask, uint32_t want, uint32_t us)
{
	for (uint32_t waited = 0; (hc_rd(e, reg) & mask) != want; waited++) {
		if (waited >= us) {
			return -1;
		}
		emmc_udelay(1);
	}
	return 0;
}

/*
 * Qualcomm's controller asks software, through its "power irq", whether a change of bus power or
 * of I/O voltage has been made, and holds the change until it is told so. Nothing takes that
 * interrupt here, so the request is looked for and acknowledged after every such change.
 */
static void
msm_power_ack(struct emmc *e, uint32_t us)
{
	uint32_t status = 0;

	if (e->core == 0) {
		return;
	}
	for (uint32_t waited = 0;; waited += 10) {
		status = emmc_rd32(e->core + MSM_PWRCTL_STATUS);
		if (status != 0 || waited >= us) {
			break;
		}
		emmc_udelay(10);
	}
	if (status == 0) {
		return;
	}
	emmc_wr32(e->core + MSM_PWRCTL_CLEAR, status);
	emmc_wr32(e->core + MSM_PWRCTL_CTL, ((status & 3) ? 1u : 0) | ((status & 0xc) ? 4u : 0));
	e->power_acks++;
}

static int
emmc_reset(struct emmc *e, uint32_t which)
{
	hc_wr(e, SDHCI_CLOCK, hc_rd(e, SDHCI_CLOCK) | which);
	if (emmc_wait(e, SDHCI_CLOCK, which, 0, 100000) != 0) {
		emmc_log("%s: the controller does not come out of reset (%x)\n", e->name, hc_rd(e, SDHCI_CLOCK));
		return -1;
	}
	return 0;
}

static int
emmc_set_clock(struct emmc *e, uint32_t hz)
{
	uint32_t divider = 0, clock;

	if (hz < e->base_hz) {
		for (divider = 1; divider < 0x3ff && e->base_hz / (2 * divider) > hz; divider++) {
		}
	}
	hc_wr(e, SDHCI_CLOCK, hc_rd(e, SDHCI_CLOCK) & ~0xffffu);
	clock = (divider & 0xff) << 8 | ((divider >> 8) & 3) << 6 | CLOCK_INTERNAL;
	hc_wr(e, SDHCI_CLOCK, (hc_rd(e, SDHCI_CLOCK) & ~0xfffffu) | clock | TIMEOUT_MAX);
	if (emmc_wait(e, SDHCI_CLOCK, CLOCK_STABLE, CLOCK_STABLE, 100000) != 0) {
		emmc_log("%s: the clock does not settle\n", e->name);
		return -1;
	}
	hc_wr(e, SDHCI_CLOCK, hc_rd(e, SDHCI_CLOCK) | CLOCK_CARD);
	e->hz = divider ? e->base_hz / (2 * divider) : e->base_hz;
	emmc_udelay(1000);
	return 0;
}

/* One command. `response` gets 1 word (48-bit responses) or 4 (136-bit). Returns 0 or -1. */
static int
emmc_command(struct emmc *e, uint32_t index, uint32_t argument, uint32_t flags, uint32_t mode, uint32_t *response)
{
	uint32_t status, inhibit = PRESENT_CMD_INHIBIT;

	if ((flags & 3) == RESP_48_BUSY || (flags & CMD_DATA)) {
		inhibit |= PRESENT_DAT_INHIBIT;
	}
	if (emmc_wait(e, SDHCI_PRESENT, inhibit, 0, 100000) != 0) {
		emmc_log("%s: CMD%u: the controller stays busy (%x)\n", e->name, index, hc_rd(e, SDHCI_PRESENT));
		emmc_reset(e, RESET_CMD | RESET_DATA);
		return -1;
	}
	hc_wr(e, SDHCI_INT_STATUS, 0xffffffffu);
	hc_wr(e, SDHCI_ARGUMENT, argument);
	hc_wr(e, SDHCI_COMMAND, (index << 8 | flags) << 16 | mode);
	for (uint32_t waited = 0;; waited++) {
		status = hc_rd(e, SDHCI_INT_STATUS);
		if (status & (INT_COMMAND_DONE | INT_ERROR)) {
			break;
		}
		/* a power-control request of Qualcomm's core holds everything until it is answered */
		if ((waited & 1023) == 1023) {
			msm_power_ack(e, 0);
		}
		if (waited >= 1000000) {
			emmc_log("%s: CMD%u: no answer (status %x, present %x)\n", e->name, index, hc_rd(e, SDHCI_INT_STATUS),
			    hc_rd(e, SDHCI_PRESENT));
			emmc_reset(e, RESET_CMD | RESET_DATA);
			return -1;
		}
		emmc_udelay(1);
	}
	if (status & INT_ERROR) {
		e->last_error = status & INT_ERRORS;
		hc_wr(e, SDHCI_INT_STATUS, status);
		emmc_reset(e, RESET_CMD | RESET_DATA);
		return -1;
	}
	hc_wr(e, SDHCI_INT_STATUS, INT_COMMAND_DONE);
	if (response != NULL) {
		response[0] = hc_rd(e, SDHCI_RESPONSE);
		if ((flags & 3) == RESP_136) {
			response[1] = hc_rd(e, SDHCI_RESPONSE + 4);
			response[2] = hc_rd(e, SDHCI_RESPONSE + 8);
			response[3] = hc_rd(e, SDHCI_RESPONSE + 12);
		}
	}
	if ((flags & 3) == RESP_48_BUSY) {
		if (emmc_wait(e, SDHCI_INT_STATUS, INT_TRANSFER_DONE | INT_ERROR, INT_TRANSFER_DONE, 1000000) != 0 &&
		    emmc_wait(e, SDHCI_PRESENT, PRESENT_DAT_INHIBIT, 0, 1000000) != 0) {
			emmc_log("%s: CMD%u: the card stays busy\n", e->name, index);
			return -1;
		}
		hc_wr(e, SDHCI_INT_STATUS, INT_TRANSFER_DONE);
	}
	return 0;
}

/*
 * Data: `count` blocks of 512 bytes. Reads fill `buffer`, writes take from it. The words go through
 * the data port as they come; a 32-bit buffer is not required (bytes are assembled).
 */
static int
emmc_data(struct emmc *e, uint32_t index, uint32_t argument, uint32_t count, uint8_t *buffer, int write)
{
	uint32_t mode = (write ? 0 : MODE_READ) | MODE_BLOCK_COUNT, status;

	if (count > 1) {
		mode |= MODE_MULTI | MODE_AUTO_CMD12;
	}
	hc_wr(e, SDHCI_BLOCK, count << 16 | 512);
	if (emmc_command(e, index, argument, R1 | CMD_DATA, mode, NULL) != 0) {
		e->errors++;
		return -1;
	}
	for (uint32_t block = 0; block < count; block++) {
		uint32_t ready = write ? INT_WRITE_READY : INT_READ_READY;

		for (uint32_t waited = 0;; waited++) {
			status = hc_rd(e, SDHCI_INT_STATUS);
			if (status & (ready | INT_ERROR)) {
				break;
			}
			if (waited >= 2000000) {
				emmc_log("%s: CMD%u: block %u of %u never came\n", e->name, index, block, count);
				goto failed;
			}
			emmc_udelay(1);
		}
		if (status & INT_ERROR) {
			e->last_error = status & INT_ERRORS;
			emmc_log("%s: CMD%u: error %x at block %u of %u\n", e->name, index, e->last_error >> 16, block, count);
			goto failed;
		}
		hc_wr(e, SDHCI_INT_STATUS, ready);
		for (uint32_t i = 0; i < 512; i += 4) {
			uint8_t *p = buffer + 512 * block + i;

			if (write) {
				hc_wr(e, SDHCI_DATA, (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
			} else {
				uint32_t word = hc_rd(e, SDHCI_DATA);

				p[0] = (uint8_t)word;
				p[1] = (uint8_t)(word >> 8);
				p[2] = (uint8_t)(word >> 16);
				p[3] = (uint8_t)(word >> 24);
			}
		}
	}
	if (emmc_wait(e, SDHCI_INT_STATUS, INT_TRANSFER_DONE, INT_TRANSFER_DONE, 2000000) != 0) {
		emmc_log("%s: CMD%u: the transfer does not end\n", e->name, index);
		goto failed;
	}
	hc_wr(e, SDHCI_INT_STATUS, INT_TRANSFER_DONE);
	return 0;
failed:
	e->errors++;
	hc_wr(e, SDHCI_INT_STATUS, 0xffffffffu);
	emmc_reset(e, RESET_CMD | RESET_DATA);
	if (count > 1) {
		emmc_command(e, 12, 0, R1B, 0, NULL);  /* stop the card's transfer, if it is still in one */
	}
	return -1;
}

/*
 * The SDCC1 core clock as LK left it -- it is never switched off before the kernel starts, and
 * changing it from here once hung the phone (the controller stops answering its own registers
 * while its clock is between two sources) -- read from its clock generator: source, divider and
 * M/N. The SDHCI divider then works from that. Only if a branch is off is it switched on.
 */
#define GCC_SDCC1_M             0x4200c
#define GCC_SDCC1_N             0x42010

static uint32_t
msm_clocks(struct emmc *e)
{
	/* msm8953-clock.c: xo, gpll0, gpll4, -, gpll0_out_main_div2 */
	static const uint32_t source_hz[8] = { 19200000, 800000000, 1152000000, 0, 400000000, 0, 0, 0 };
	uint32_t cfg, src, div, mode, m, n;
	uint64_t hz;

	if (e->gcc == 0) {
		return 0;
	}
	emmc_log("%s: reading the SDCC1 clock registers\n", e->name);
	if (emmc_rd32(e->gcc + GCC_SDCC1_AHB_CBCR) & CBCR_OFF) {
		emmc_wr32(e->gcc + GCC_SDCC1_AHB_CBCR, emmc_rd32(e->gcc + GCC_SDCC1_AHB_CBCR) | CBCR_ENABLE);
	}
	if (emmc_rd32(e->gcc + GCC_SDCC1_APPS_CBCR) & CBCR_OFF) {
		emmc_wr32(e->gcc + GCC_SDCC1_APPS_CBCR, emmc_rd32(e->gcc + GCC_SDCC1_APPS_CBCR) | CBCR_ENABLE);
	}
	cfg = emmc_rd32(e->gcc + GCC_SDCC1_CFG_RCGR);
	src = (cfg >> 8) & 7;
	div = cfg & 0x1f;
	mode = (cfg >> 12) & 3;
	hz = source_hz[src];
	if (div != 0) {
		hz = hz * 2 / (div + 1);
	}
	if (mode != 0) {
		m = emmc_rd32(e->gcc + GCC_SDCC1_M) & 0xff;
		n = ((~emmc_rd32(e->gcc + GCC_SDCC1_N)) & 0xff) + m;
		if (m != 0 && n != 0) {
			hz = hz * m / n;
		}
	}
	emmc_log("%s: clocks as LK left them: ahb %x, apps %x, rcg %x (source %u, divider %u, M/N mode %u): %u Hz\n",
	    e->name, emmc_rd32(e->gcc + GCC_SDCC1_AHB_CBCR), emmc_rd32(e->gcc + GCC_SDCC1_APPS_CBCR), cfg, src, div, mode,
	    (uint32_t)hz);
	return (uint32_t)hz;
}

static int
emmc_sd_identify(struct emmc *e, uint32_t *r)
{
	uint32_t ocr = 0;

	e->sd = 1;
	if (emmc_command(e, 8, 0x1aa, R1, 0, r) != 0 || (r[0] & 0xfff) != 0x1aa) {
		emmc_log("%s: neither an eMMC nor a version 2 SD card answers\n", e->name);
		return -1;
	}
	for (int tries = 0; tries < 1000; tries++) {
		if (emmc_command(e, 55, 0, R1, 0, r) != 0 || emmc_command(e, 41, 0x40ff8000, R3, 0, r) != 0) {
			return -1;
		}
		ocr = r[0];
		if (ocr & 0x80000000u) {
			break;
		}
		emmc_udelay(1000);
	}
	if (!(ocr & 0x80000000u)) {
		emmc_log("%s: the SD card stays busy (OCR %x)\n", e->name, ocr);
		return -1;
	}
	e->sectors_addressed = (ocr >> 30) & 1;
	return 0;
}

/*
 * Bring the controller and the card up, to the transfer state with the user area selected.
 * Returns 0 or -1; what happened is in the log.
 */
static int
emmc_init(struct emmc *e)
{
	uint32_t r[4], ocr = 0, caps, core_hz = 0;
	static uint8_t ext_csd[512];

	e->sd = 0;
	e->errors = 0;
	if (e->core != 0) {
		core_hz = msm_clocks(e);
		/*
		 * The whole core reset first (MCI_POWER's CORE_SW_RST), as Linux's sdhci-msm does: LK leaves
		 * HS400 behind in Qualcomm's own registers (clock selection, DLL), which an SDHCI reset
		 * does not touch. The core is in its old (MCI) mode after it.
		 */
		emmc_log("%s: Qualcomm core: version %x, MCI_POWER %x, HC_MODE %x; resetting it\n", e->name,
		    emmc_rd32(e->core + MSM_VERSION), emmc_rd32(e->core + 0x00), emmc_rd32(e->core + MSM_HC_MODE));
		emmc_wr32(e->core + 0x00, emmc_rd32(e->core + 0x00) | 1u << 7);
		for (int i = 0; i < 10000 && (emmc_rd32(e->core + 0x00) & 1u << 7); i++) {
			emmc_udelay(1);
		}
		emmc_log("%s: core reset: MCI_POWER %x\n", e->name, emmc_rd32(e->core + 0x00));
		hc_wr(e, MSM_VENDOR_SPEC, 0xa1c);                       /* as LK writes it first */
		emmc_wr32(e->core + MSM_HC_MODE, emmc_rd32(e->core + MSM_HC_MODE) | 1u | 1u << 13);
		emmc_log("%s: Qualcomm SDCC %x, SDHCI mode on\n", e->name, emmc_rd32(e->core + MSM_VERSION));
	}
	emmc_log("%s: SDHCI reset (present %x)\n", e->name, hc_rd(e, SDHCI_PRESENT));
	if (emmc_reset(e, RESET_ALL) != 0) {
		return -1;
	}
	if (e->core != 0) {
		/* a reset raises a request if the bus was on: acknowledged, then all four asked for */
		msm_power_ack(e, 1000);
		emmc_wr32(e->core + MSM_PWRCTL_MASK, 0xf);
		/* the voltages are not in the SDHCI capabilities of this core: as LK says them (1.8 V) */
		hc_wr(e, MSM_VENDOR_CAPS0, hc_rd(e, SDHCI_CAPS) | 1u << 26);
	}
	caps = hc_rd(e, SDHCI_CAPS);
	e->base_hz = ((caps >> 8) & 0xff) * 1000000u;
	if (e->core != 0 && core_hz != 0) {
		e->base_hz = core_hz;
	} else if (e->base_hz == 0) {
		e->base_hz = 50000000u;
	}
	emmc_log("%s: SDHCI version %u, capabilities %x, base clock %u Hz, present %x\n", e->name,
	    ((hc_rd(e, SDHCI_VERSION) >> 16) & 0xff) + 1, caps, e->base_hz, hc_rd(e, SDHCI_PRESENT));

	hc_wr(e, SDHCI_INT_ENABLE, 0xffffffffu & ~(1u << 8));         /* every status but the card interrupt; none signalled */
	hc_wr(e, SDHCI_SIGNAL_ENABLE, 0);
	/* bus power: 1.8 V for the Mi A1's eMMC (it signals at 1.8 V), 3.3 V for an SD card */
	hc_wr(e, SDHCI_HOST_CONTROL, (hc_rd(e, SDHCI_HOST_CONTROL) & ~0xff00u) | (e->core ? POWER_180 : POWER_330));
	hc_wr(e, SDHCI_HOST_CONTROL, hc_rd(e, SDHCI_HOST_CONTROL) | POWER_ON);
	msm_power_ack(e, 10000);
	emmc_udelay(10000);
	if (emmc_set_clock(e, 400000) != 0) {
		return -1;
	}

	emmc_command(e, 0, 0, RESP_NONE, 0, NULL);                      /* from sleep (or anything) to idle */
	emmc_udelay(2000);
	/* eMMC: CMD1 until it is ready; sector addressing, 1.7-1.95 V and 2.7-3.6 V */
	for (int tries = 0; tries < 1000; tries++) {
		if (emmc_command(e, 1, 0x40ff8080, R3, 0, r) != 0) {
			break;
		}
		ocr = r[0];
		if (ocr & 0x80000000u) {
			break;
		}
		emmc_udelay(1000);
	}
	if (ocr & 0x80000000u) {
		e->sectors_addressed = (ocr >> 30) & 1;
	} else {
		emmc_command(e, 0, 0, RESP_NONE, 0, NULL);
		emmc_udelay(2000);
		if (emmc_sd_identify(e, r) != 0) {
			return -1;
		}
	}
	if (emmc_command(e, 2, 0, R2, 0, e->cid) != 0) {
		emmc_log("%s: CMD2 (CID) failed\n", e->name);
		return -1;
	}
	if (e->sd) {
		if (emmc_command(e, 3, 0, R1, 0, r) != 0) {
			return -1;
		}
		e->rca = r[0] >> 16;
	} else {
		e->rca = 1;
		if (emmc_command(e, 3, e->rca << 16, R1, 0, r) != 0) {
			return -1;
		}
	}
	if (emmc_command(e, 9, e->rca << 16, R2, 0, r) != 0) {
		emmc_log("%s: CMD9 (CSD) failed\n", e->name);
		return -1;
	}
	if (e->sd) {
		/* CSD version 2: C_SIZE is bits 69:48 of the CSD, 61:40 of what SDHCI keeps (no CRC) */
		uint64_t c_size = ((uint64_t)r[1] >> 8 | (uint64_t)r[2] << 24) & 0x3fffff;

		e->sectors = (c_size + 1) * 1024;
	}
	if (emmc_command(e, 7, e->rca << 16, R1B, 0, r) != 0) {
		emmc_log("%s: CMD7 (select) failed\n", e->name);
		return -1;
	}
	if (emmc_set_clock(e, 25000000) != 0) {
		return -1;
	}
	e->width = 1;
	if (e->sd) {
		if (emmc_command(e, 55, e->rca << 16, R1, 0, r) == 0 && emmc_command(e, 6, 2, R1, 0, r) == 0) {
			hc_wr(e, SDHCI_HOST_CONTROL, hc_rd(e, SDHCI_HOST_CONTROL) | HOST_4BIT);
			e->width = 4;
		}
	} else {
		if (emmc_data(e, 8, 0, 1, ext_csd, 0) != 0) {
			emmc_log("%s: CMD8 (EXT_CSD) failed\n", e->name);
			return -1;
		}
		e->sectors = (uint32_t)ext_csd[EXT_CSD_SECTORS] | (uint32_t)ext_csd[EXT_CSD_SECTORS + 1] << 8 |
		    (uint32_t)ext_csd[EXT_CSD_SECTORS + 2] << 16 | (uint32_t)ext_csd[EXT_CSD_SECTORS + 3] << 24;
		emmc_log("%s: EXT_CSD revision %u, partition config %x, bus width %u, timing %u\n", e->name,
		    ext_csd[EXT_CSD_REV], ext_csd[EXT_CSD_PARTITION_CONFIG], ext_csd[EXT_CSD_BUS_WIDTH], ext_csd[EXT_CSD_HS_TIMING]);
		/* the user area, whatever the bootloader left selected (it reads the boot areas too) */
		if (ext_csd[EXT_CSD_PARTITION_CONFIG] & 7) {
			emmc_command(e, 6, 3u << 24 | EXT_CSD_PARTITION_CONFIG << 16 |
			    (uint32_t)(ext_csd[EXT_CSD_PARTITION_CONFIG] & ~7u) << 8, R1B, 0, r);
		}
		/* 8 lines, then high speed timing at 50 MHz */
		if (emmc_command(e, 6, 3u << 24 | EXT_CSD_BUS_WIDTH << 16 | 2u << 8, R1B, 0, r) == 0) {
			hc_wr(e, SDHCI_HOST_CONTROL, (hc_rd(e, SDHCI_HOST_CONTROL) & ~HOST_4BIT) | HOST_8BIT);
			e->width = 8;
		}
		if (emmc_command(e, 6, 3u << 24 | EXT_CSD_HS_TIMING << 16 | 1u << 8, R1B, 0, r) == 0) {
			hc_wr(e, SDHCI_HOST_CONTROL, hc_rd(e, SDHCI_HOST_CONTROL) | HOST_HIGH_SPEED);
			emmc_set_clock(e, 50000000);
		}
	}
	if (!e->sectors_addressed && e->sd == 0 && e->sectors == 0) {
		emmc_log("%s: a card of the old byte-addressed kind: not supported\n", e->name);
		return -1;
	}
	emmc_command(e, 16, 512, R1, 0, r);                            /* ignored by high capacity cards */
	if (emmc_command(e, 13, e->rca << 16, R1, 0, r) != 0) {
		emmc_log("%s: CMD13 (status) failed\n", e->name);
		return -1;
	}
	emmc_log("%s: %s ready: %llu MB, %u data lines at %u Hz, CID %x %x %x %x, status %x\n", e->name,
	    e->sd ? "SD card" : "eMMC", (unsigned long long)(e->sectors / 2048), e->width, e->hz, e->cid[3], e->cid[2],
	    e->cid[1], e->cid[0], r[0]);
	return 0;
}

/* Read or write `count` sectors from `sector` on. */
static int
emmc_io(struct emmc *e, uint64_t sector, uint32_t count, uint8_t *buffer, int write)
{
	while (count > 0) {
		uint32_t now = count > 2048 ? 2048 : count;
		uint32_t argument = (uint32_t)(e->sectors_addressed ? sector : sector * 512);
		int done = -1;

		for (int attempt = 0; attempt < 3 && done != 0; attempt++) {
			done = emmc_data(e, write ? (now > 1 ? 25 : 24) : (now > 1 ? 18 : 17), argument, now, buffer, write);
		}
		if (done != 0) {
			return -1;
		}
		if (write) {
			e->writes++;
			e->written_sectors += now;
		} else {
			e->reads++;
			e->read_sectors += now;
		}
		sector += now;
		count -= now;
		buffer += 512 * (uint64_t)now;
	}
	return 0;
}

/* ---- GPT --------------------------------------------------------------------------------------- */

struct emmc_part {
	char            name[37];
	uint64_t        first, sectors;
};

/*
 * Every partition of the GPT, to `each` (which returns nonzero to stop there). `sector` is a buffer
 * of 512 bytes. Returns what `each` returned last, 0 at the end of the table, or -1.
 */
static int
emmc_gpt_walk(struct emmc *e, uint8_t *sector, int (*each)(void *context, const struct emmc_part *part), void *context)
{
	uint64_t entries_lba = 0;
	uint32_t entries, entry_size;
	struct emmc_part part;

	if (emmc_io(e, 1, 1, sector, 0) != 0) {
		return -1;
	}
	if (sector[0] != 'E' || sector[1] != 'F' || sector[2] != 'I' || sector[3] != ' ' || sector[4] != 'P') {
		emmc_log("%s: no GPT\n", e->name);
		return -1;
	}
	for (int i = 7; i >= 0; i--) {
		entries_lba = entries_lba << 8 | sector[72 + i];
	}
	entries = (uint32_t)sector[80] | (uint32_t)sector[81] << 8 | (uint32_t)sector[82] << 16 | (uint32_t)sector[83] << 24;
	entry_size = (uint32_t)sector[84] | (uint32_t)sector[85] << 8;
	if (entry_size < 128 || entry_size > 512 || 512 % entry_size != 0 || entries > 1024) {
		emmc_log("%s: a GPT this code does not read (%u entries of %u bytes)\n", e->name, entries, entry_size);
		return -1;
	}
	for (uint32_t n = 0; n < entries; n++) {
		uint32_t at = (n * entry_size) % 512;
		uint64_t first = 0, last = 0;
		const uint8_t *p;
		int i, stop;

		if (at == 0 && emmc_io(e, entries_lba + n * entry_size / 512, 1, sector, 0) != 0) {
			return -1;
		}
		p = sector + at;
		for (i = 7; i >= 0; i--) {
			first = first << 8 | p[32 + i];
			last = last << 8 | p[40 + i];
		}
		if (first == 0 || last < first) {
			continue;               /* an unused entry */
		}
		for (i = 0; i < 36; i++) {
			part.name[i] = p[57 + 2 * i] != 0 ? '?' : (char)p[56 + 2 * i];
		}
		part.name[36] = 0;
		part.first = first;
		part.sectors = last - first + 1;
		stop = each(context, &part);
		if (stop != 0) {
			return stop;
		}
	}
	return 0;
}

struct emmc_find {
	const char *            name;
	struct emmc_part *      part;
};

static int
emmc_find_each(void *context, const struct emmc_part *part)
{
	struct emmc_find *find = context;
	int i;

	for (i = 0; find->name[i] != 0 && find->name[i] == part->name[i]; i++) {
	}
	if (find->name[i] != 0 || part->name[i] != 0) {
		return 0;
	}
	*find->part = *part;
	return 1;
}

/* A partition by name (ASCII, against the GPT's UTF-16 name). Returns 0 and fills `part`, or -1. */
static int
emmc_find_partition(struct emmc *e, const char *name, struct emmc_part *part, uint8_t *sector)
{
	struct emmc_find find = { .name = name, .part = part };
	int found = emmc_gpt_walk(e, sector, emmc_find_each, &find);

	if (found == 0) {
		emmc_log("%s: no partition named %s\n", e->name, name);
	}
	return found == 1 ? 0 : -1;
}

#endif
