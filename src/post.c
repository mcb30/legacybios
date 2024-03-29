// 32bit code to Power On Self Test (POST) a machine.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "ioport.h" // PORT_*
#include "../out/rom16.offset.auto.h" // OFFSET_*
#include "config.h" // CONFIG_*
#include "cmos.h" // CMOS_*
#include "util.h" // memset
#include "biosvar.h" // struct bios_data_area_s
#include "ata.h"
#include "kbd.h"

#define bda ((struct bios_data_area_s *)0)
#define ebda ((struct extended_bios_data_area_s *)(EBDA_SEG<<4))

static void
init_bda()
{
    memset(bda, 0, sizeof(*bda));

    int i;
    for (i=0; i<256; i++) {
        SET_BDA(ivecs[i].seg, SEG_BIOS);
        SET_BDA(ivecs[i].offset, OFFSET_dummy_iret_handler);
    }

    SET_BDA(mem_size_kb, BASE_MEM_IN_K);

    // mov CMOS Equipment Byte to BDA Equipment Word
    SET_BDA(equipment_list_flags, inb_cmos(CMOS_EQUIPMENT_INFO));
}

static void
init_handlers()
{
    // set vector 0x79 to zero
    // this is used by 'gardian angel' protection system
    SET_BDA(ivecs[0x79].seg, 0);
    SET_BDA(ivecs[0x79].offset, 0);

    SET_BDA(ivecs[0x40].offset, OFFSET_entry_40);
    SET_BDA(ivecs[0x0e].offset, OFFSET_entry_0e);
    SET_BDA(ivecs[0x13].offset, OFFSET_entry_13);
    SET_BDA(ivecs[0x76].offset, OFFSET_entry_76);
    SET_BDA(ivecs[0x17].offset, OFFSET_entry_17);
    SET_BDA(ivecs[0x18].offset, OFFSET_entry_18);
    SET_BDA(ivecs[0x19].offset, OFFSET_entry_19);
    SET_BDA(ivecs[0x1c].offset, OFFSET_entry_1c);
    SET_BDA(ivecs[0x12].offset, OFFSET_entry_12);
    SET_BDA(ivecs[0x11].offset, OFFSET_entry_11);
    SET_BDA(ivecs[0x15].offset, OFFSET_entry_15);
    SET_BDA(ivecs[0x08].offset, OFFSET_entry_08);
    SET_BDA(ivecs[0x09].offset, OFFSET_entry_09);
    SET_BDA(ivecs[0x16].offset, OFFSET_entry_16);
    SET_BDA(ivecs[0x14].offset, OFFSET_entry_14);
    SET_BDA(ivecs[0x1a].offset, OFFSET_entry_1a);
    SET_BDA(ivecs[0x70].offset, OFFSET_entry_70);
    SET_BDA(ivecs[0x74].offset, OFFSET_entry_74);
    SET_BDA(ivecs[0x75].offset, OFFSET_entry_75);
    SET_BDA(ivecs[0x10].offset, OFFSET_entry_10);
}

static void
init_ebda()
{
    memset(ebda, 0, sizeof(*ebda));
    ebda->size = EBDA_SIZE;
    SET_BDA(ebda_seg, EBDA_SEG);
    SET_BDA(ivecs[0x41].seg, EBDA_SEG);
    SET_BDA(ivecs[0x41].offset
            , offsetof(struct extended_bios_data_area_s, fdpt0));
    SET_BDA(ivecs[0x46].seg, EBDA_SEG);
    SET_BDA(ivecs[0x41].offset
            , offsetof(struct extended_bios_data_area_s, fdpt1));
}

static void
pit_setup()
{
    // timer0: binary count, 16bit count, mode 2
    outb(0x34, PORT_PIT_MODE);
    // maximum count of 0000H = 18.2Hz
    outb(0x0, PORT_PIT_COUNTER0);
    outb(0x0, PORT_PIT_COUNTER0);
}

static u16
detect_parport(u16 port, u8 timeout, u8 count)
{
    // clear input mode
    outb(inb(port+2) & 0xdf, port+2);

    outb(0xaa, port);
    if (inb(port) != 0xaa)
        // Not present
        return 0;
    SET_BDA(port_lpt[count], port);
    SET_BDA(lpt_timeout[count], timeout);
    return 1;
}

static void
lpt_setup()
{
    u16 count = 0;
    count += detect_parport(0x378, 0x14, count);
    count += detect_parport(0x278, 0x14, count);

    // Equipment word bits 14..15 determing # parallel ports
    u16 eqb = GET_BDA(equipment_list_flags);
    SET_BDA(equipment_list_flags, (eqb & 0x3fff) | (count << 14));
}

static u16
detect_serial(u16 port, u8 timeout, u8 count)
{
    outb(0x02, port+1);
    if (inb(port+1) != 0x02)
        return 0;
    if (inb(port+2) != 0x02)
        return 0;
    outb(0x00, port+1);
    SET_BDA(port_com[count], port);
    SET_BDA(com_timeout[count], timeout);
    return 1;
}

static void
serial_setup()
{
    u16 count = 0;
    count += detect_serial(0x3f8, 0x0a, count);
    count += detect_serial(0x2f8, 0x0a, count);
    count += detect_serial(0x3e8, 0x0a, count);
    count += detect_serial(0x2e8, 0x0a, count);

    // Equipment word bits 9..11 determing # serial ports
    u16 eqb = GET_BDA(equipment_list_flags);
    SET_BDA(equipment_list_flags, (eqb & 0xf1ff) | (count << 9));
}

static u32
bcd2bin(u8 val)
{
    return (val & 0xf) + ((val >> 4) * 10);
}

static void
timer_setup()
{
    u32 seconds = bcd2bin(inb_cmos(CMOS_RTC_SECONDS));
    u32 ticks = (seconds * 18206507) / 1000000;
    u32 minutes = bcd2bin(inb_cmos(CMOS_RTC_MINUTES));
    ticks += (minutes * 10923904) / 10000;
    u32 hours = bcd2bin(inb_cmos(CMOS_RTC_HOURS));
    ticks += (hours * 65543427) / 1000;
    SET_BDA(timer_counter, ticks);
    SET_BDA(timer_rollover, 0);
}

static void
pic_setup()
{
    outb(0x11, PORT_PIC1);
    outb(0x11, PORT_PIC2);
    outb(0x08, PORT_PIC1_DATA);
    outb(0x70, PORT_PIC2_DATA);
    outb(0x04, PORT_PIC1_DATA);
    outb(0x02, PORT_PIC2_DATA);
    outb(0x01, PORT_PIC1_DATA);
    outb(0x01, PORT_PIC2_DATA);
    outb(0xb8, PORT_PIC1_DATA);
    if (CONFIG_PS2_MOUSE)
        outb(0x8f, PORT_PIC2_DATA);
    else
        outb(0x9f, PORT_PIC2_DATA);
}

static void
floppy_drive_post()
{
    u8 type = inb_cmos(CMOS_FLOPPY_DRIVE_TYPE);
    u8 out = 0;
    if (type & 0xf0)
        out |= 0x07;
    if (type & 0x0f)
        out |= 0x70;
    SET_BDA(floppy_harddisk_info, out);
    outb(0x02, PORT_DMA1_MASK_REG);

    SET_BDA(ivecs[0x1E].offset, OFFSET_diskette_param_table2);
}

static void
ata_init()
{
    // hdidmap  and cdidmap init.
    u8 device;
    for (device=0; device < CONFIG_MAX_ATA_DEVICES; device++) {
        ebda->ata.idmap[0][device] = CONFIG_MAX_ATA_DEVICES;
        ebda->ata.idmap[1][device] = CONFIG_MAX_ATA_DEVICES;
    }
}

static void
fill_hdinfo(struct fdpt_s *info, u8 typecmos, u8 basecmos)
{
    u8 type = inb_cmos(typecmos);
    if (type != 47)
        // XXX - halt
        return;

    info->precompensation = (inb_cmos(basecmos+4) << 8) | inb_cmos(basecmos+3);
    info->drive_control_byte = inb_cmos(basecmos+5);
    info->landing_zone = (inb_cmos(basecmos+7) << 8) | inb_cmos(basecmos+6);
    u16 cyl = (inb_cmos(basecmos+1) << 8) | inb_cmos(basecmos+0);
    u8 heads = inb_cmos(basecmos+2);
    u8 sectors = inb_cmos(basecmos+8);
    if (cyl < 1024) {
        // no logical CHS mapping used, just physical CHS
        // use Standard Fixed Disk Parameter Table (FDPT)
        info->cylinders = cyl;
        info->heads = heads;
        info->sectors = sectors;
        return;
    }

    // complies with Phoenix style Translated Fixed Disk Parameter
    // Table (FDPT)
    info->phys_cylinders = cyl;
    info->phys_heads = heads;
    info->phys_sectors = sectors;
    info->sectors = sectors;
    info->a0h_signature = 0xa0;
    if (cyl > 8192) {
        cyl >>= 4;
        heads <<= 4;
    } else if (cyl > 4096) {
        cyl >>= 3;
        heads <<= 3;
    } else if (cyl > 2048) {
        cyl >>= 2;
        heads <<= 2;
    }
    info->cylinders = cyl;
    info->heads = heads;
    info->checksum = -checksum((u8*)info, sizeof(*info)-1);
}

static void
hard_drive_post()
{
    outb(0x0a, 0x03f6); // 0000 1010 = reserved, disable IRQ 14
    SET_BDA(disk_count, 1);
    SET_BDA(disk_control_byte, 0xc0);

    // move disk geometry data from CMOS to EBDA disk parameter table(s)
    u8 diskinfo = inb_cmos(CMOS_DISK_DATA);
    if ((diskinfo & 0xf0) == 0xf0)
        // Fill EBDA table for hard disk 0.
        fill_hdinfo(&ebda->fdpt0, CMOS_DISK_DRIVE1_TYPE, CMOS_DISK_DRIVE1_CYL);
    if ((diskinfo & 0x0f) == 0x0f)
        // XXX - bochs halts on any other type
        // Fill EBDA table for hard disk 1.
        fill_hdinfo(&ebda->fdpt0, CMOS_DISK_DRIVE2_TYPE, CMOS_DISK_DRIVE2_CYL);
}


static void
init_boot_vectors()
{
    // Floppy drive
    struct ipl_entry_s *ip = &ebda->ipl.table[0];
    ip->type = IPL_TYPE_FLOPPY;
    ip++;

    // First HDD
    ip->type = IPL_TYPE_HARDDISK;
    ip++;

    // CDROM
    if (CONFIG_CDROM_BOOT) {
        ip->type = IPL_TYPE_CDROM;
        ip++;
    }

    ebda->ipl.count = ip - ebda->ipl.table;
    ebda->ipl.sequence = 0xffff;
}

static void
callrom(u16 seg, u16 offset)
{
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.es = SEG_BIOS;
    br.di = OFFSET_pnp_string + 1; // starts 1 past for alignment
    br.cs = seg;
    br.ip = offset;
    call16(&br);
}

static void
rom_scan(u32 start, u32 end)
{
    u8 *p = (u8*)start;
    for (; p <= (u8*)end; p += 2048) {
        u8 *rom = p;
        if (*(u16*)rom != 0xaa55)
            continue;
        u32 len = rom[2] * 512;
        if (checksum(rom, len) != 0)
            continue;
        p = (u8*)(((u32)p + len) / 2048 * 2048);
        callrom(FARPTR_TO_SEG(rom), FARPTR_TO_OFFSET(rom + 3));

        // Look at the ROM's PnP Expansion header.  Properly, we're supposed
        // to init all the ROMs and then go back and build an IPL table of
        // all the bootable devices, but we can get away with one pass.
        if (rom[0x1a] != '$' || rom[0x1b] != 'P'
            || rom[0x1c] != 'n' || rom[0x1d] != 'P')
            continue;
        // 0x1A is also the offset into the expansion header of...
        // the Bootstrap Entry Vector, or zero if there is none.
        u16 entry = *(u16*)&rom[0x1a+0x1a];
        if (!entry)
            continue;
        // Found a device that thinks it can boot the system.  Record
        // its BEV and product name string.

        if (ebda->ipl.count >= ARRAY_SIZE(ebda->ipl.table))
            continue;

        struct ipl_entry_s *ip = &ebda->ipl.table[ebda->ipl.count];
        ip->type = IPL_TYPE_BEV;
        ip->vector = (FARPTR_TO_SEG(rom) << 16) | entry;

        u16 desc = *(u16*)&rom[0x1a+0x10];
        if (desc)
            ip->description = (FARPTR_TO_SEG(rom) << 16) | desc;

        ebda->ipl.count++;
    }
}

static void
post()
{
    BX_INFO("Start bios\n");

    init_bda();
    init_handlers();
    init_ebda();

    pit_setup();
    kbd_setup();
    lpt_setup();
    serial_setup();
    timer_setup();
    pic_setup();

    rom_scan(0xc0000, 0xc7800);

    printf("BIOS - begin\n\n");

    // clear bss section -- XXX - shouldn't use globals
    extern char __bss_start[], __bss_end[];
    memset(__bss_start, 0, __bss_end - __bss_start);

    rombios32_init();

    init_boot_vectors();

    floppy_drive_post();
    hard_drive_post();
    if (CONFIG_ATA) {
        ata_init();
        ata_detect();
    }

    init_boot_vectors();

    rom_scan(0xc8000, 0xe0000);

    // reset the memory (some boot loaders such as syslinux suppose
    // that the memory is set to zero)
    memset((void*)0x40000, 0, 0x40000); // XXX - shouldn't use globals

    // Invoke int 19 to start boot process.
    struct bregs br;
    memset(&br, 0, sizeof(br));
    call16_int(0x19, &br);
}

static void
init_dma()
{
    // first reset the DMA controllers
    outb(0, PORT_DMA1_MASTER_CLEAR);
    outb(0, PORT_DMA2_MASTER_CLEAR);

    // then initialize the DMA controllers
    outb(0xc0, PORT_DMA2_MODE_REG);
    outb(0x00, PORT_DMA2_MASK_REG);
}

static void
check_restart_status()
{
    // Get and then clear CMOS shutdown status.
    u8 status = inb_cmos(CMOS_RESET_CODE);
    outb_cmos(0, CMOS_RESET_CODE);

    if (status == 0x00 || status == 0x09 || status >= 0x0d)
        // Normal post
        return;

    if (status != 0x05) {
        BX_PANIC("Unimplemented shutdown status: %02x\n", status);
        return;
    }

    // XXX - this is supposed to jump without changing any memory -
    // but the stack has been altered by the time the code gets here.
    eoi_both_pics();
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.cs = GET_BDA(jump_cs_ip) >> 16;
    br.ip = GET_BDA(jump_cs_ip);
    call16(&br);
}

void VISIBLE32
_start()
{
    init_dma();
    check_restart_status();

    post();
}
