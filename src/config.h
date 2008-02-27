#ifndef __CONFIG_H
#define __CONFIG_H
// Configuration definitions.

#define CONFIG_FLOPPY_SUPPORT 1
#define CONFIG_PS2_MOUSE 0
#define CONFIG_ATA 0
#define CONFIG_KBD_CALL_INT15_4F 1
#define CONFIG_ELTORITO_BOOT 0
#define CONFIG_MAX_ATA_INTERFACES 4
#define CONFIG_MAX_ATA_DEVICES  (CONFIG_MAX_ATA_INTERFACES*2)

#define CONFIG_STACK_SEGMENT 0x00
#define CONFIG_STACK_OFFSET  0xfffe

#endif // config.h
