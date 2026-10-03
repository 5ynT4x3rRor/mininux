#ifndef MININUX_DRIVER_H
#define MININUX_DRIVER_H

#define DRV_LOADED 0U
#define DRV_NO_DRIVER 1U
#define DRV_FAILED 2U

#define DRV_MAX_RECORDS 24U

struct driver_record {
    const char *name;
    unsigned short vendor;
    unsigned short device;
    unsigned char bus;
    unsigned char slot;
    unsigned char function;
    unsigned char class_code;
    unsigned char state;
};

/* Charge au demarrage tous les pilotes dont le materiel est present. */
void drivers_load(unsigned char disk_ok, unsigned char keyboard_ok);
unsigned int driver_count(void);
const struct driver_record *driver_get(unsigned int index);
unsigned int driver_count_state(unsigned char state);

#endif
