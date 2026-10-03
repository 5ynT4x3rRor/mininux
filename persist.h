#ifndef MININUX_PERSIST_H
#define MININUX_PERSIST_H

#define MN_MAX_USERS 6U
#define MN_MAX_FILES 16U
#define MN_NAME_USER 16U
#define MN_NAME_FILE 24U
#define MN_FILE_DATA 224U
#define MN_PASSWORD_MIN 8U
#define MN_PASSWORD_MAX 40U

#define MN_ROLE_USER 0U
#define MN_ROLE_ADMIN 1U

#define MN_OK 0
#define MN_ERR_IO -1
#define MN_ERR_EXISTS -2
#define MN_ERR_FULL -3
#define MN_ERR_NOTFOUND -4
#define MN_ERR_INVALID -5
#define MN_ERR_DENIED -6

/* Acces disque BIOS (INT 13h ext.): func 0x42 lecture, 0x43 ecriture. */
unsigned int bios_disk_io(unsigned char func, unsigned int lba,
                          unsigned int count, unsigned int linear_address);

/* Retourne 0 si une copie valide est chargee, 1 si le disque est vierge,
   2 si les copies sont corrompues, -1 si le disque est illisible. */
int store_load(void);
unsigned char store_persistent(void);
/* Statut de la derniere ecriture disque (MN_OK ou MN_ERR_IO). */
int store_last_status(void);

int user_slot_count(void);
unsigned char user_slot_used(unsigned int slot);
const char *user_slot_name(unsigned int slot);
unsigned char user_slot_role(unsigned int slot);
int user_find(const char *name);
int user_add(const char *name, const char *password, unsigned char role);
int user_remove(unsigned int slot);
int user_set_password(unsigned int slot, const char *password);
unsigned char user_check_password(unsigned int slot, const char *password);
unsigned char name_is_valid(const char *name, unsigned int maximum, unsigned char allow_dot);

int file_find(const char *name);
unsigned char file_slot_used(unsigned int slot);
const char *file_slot_name(unsigned int slot);
unsigned char file_slot_owner(unsigned int slot);
unsigned char file_slot_shared(unsigned int slot);
unsigned int file_slot_length(unsigned int slot);
const char *file_slot_data(unsigned int slot);
int file_write(const char *name, const char *data, unsigned int length,
               unsigned char owner, unsigned char shared);
int file_remove(unsigned int slot);

#endif
