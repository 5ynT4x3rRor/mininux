#include "crypto.h"
#include "persist.h"

/*
 * Disposition sur le disque (hors partition declaree, secteurs 256+):
 * deux copies A/B de 9 secteurs. Chaque copie = 1 secteur de comptes
 * + 8 secteurs de fichiers. La copie valide au compteur le plus haut gagne,
 * et on ecrit toujours sur l'autre copie: une coupure en cours d'ecriture
 * laisse l'ancienne copie intacte.
 */
#define STORE_LBA_A 256U
#define STORE_LBA_B 272U
#define STORE_SECTORS 9U
#define STORE_BUFFER 0x40000U
#define STORE_MAGIC 0x53504e4dU
#define STORE_VERSION 1U
#define HASH_ITERATIONS 5000U
#define HASH_ITERATIONS_LIMIT 100000U
#define BIOS_WRITE_ENTRY 0x7d00U

struct user_record {
    unsigned char used;
    unsigned char role;
    unsigned char reserved[2];
    char name[MN_NAME_USER];
    unsigned char salt[16];
    unsigned int iterations;
    unsigned char hash[32];
};

struct account_sector {
    unsigned int magic;
    unsigned int version;
    unsigned int sequence;
    unsigned int reserved;
    struct user_record users[MN_MAX_USERS];
    unsigned char files_hash[32];
    unsigned char check[32];
};

struct file_record {
    unsigned char used;
    unsigned char owner;
    unsigned char shared;
    unsigned char reserved;
    unsigned short length;
    char name[MN_NAME_FILE];
    char data[MN_FILE_DATA];
    unsigned char padding[2];
};

typedef char account_sector_size_check[sizeof(struct account_sector) == 512 ? 1 : -1];
typedef char file_record_size_check[sizeof(struct file_record) == 256 ? 1 : -1];

static struct account_sector *const accounts = (struct account_sector *)STORE_BUFFER;
static struct file_record *const files = (struct file_record *)(STORE_BUFFER + 512);
static unsigned int active_copy_lba;
static unsigned char persistent;

unsigned int bios_disk_io(unsigned char func, unsigned int lba,
                          unsigned int count, unsigned int linear_address)
{
    volatile unsigned int *dap = (volatile unsigned int *)0x600;

    dap[0] = 0x10U | (count << 16);
    dap[1] = (linear_address & 0xfU) | ((linear_address >> 4) << 16);
    dap[2] = lba;
    dap[3] = 0;
    *(volatile unsigned char *)0x5fc = 0;
    *(volatile unsigned char *)0x5fd = func;
    ((void (*)(void))BIOS_WRITE_ENTRY)();
    return *(volatile unsigned char *)0x5fc;
}

static void compute_checks(void)
{
    sha256((const unsigned char *)files, MN_MAX_FILES * sizeof(struct file_record),
           accounts->files_hash);
    sha256((const unsigned char *)accounts, 480, accounts->check);
}

/* Les donnees du disque ne sont pas fiables: on borne tout avant usage. */
static unsigned char store_is_sane(void)
{
    unsigned char hash[32];

    if (accounts->magic != STORE_MAGIC || accounts->version != STORE_VERSION) {
        return 0;
    }
    sha256((const unsigned char *)accounts, 480, hash);
    if (!mn_equal(hash, accounts->check, 32)) {
        return 0;
    }
    sha256((const unsigned char *)files, MN_MAX_FILES * sizeof(struct file_record), hash);
    if (!mn_equal(hash, accounts->files_hash, 32)) {
        return 0;
    }
    if (!accounts->users[0].used || accounts->users[0].role != MN_ROLE_ADMIN) {
        return 0;
    }
    for (unsigned int i = 0; i < MN_MAX_USERS; i++) {
        struct user_record *user = &accounts->users[i];

        if (!user->used) {
            continue;
        }
        if (user->role > MN_ROLE_ADMIN || user->iterations == 0 ||
            user->iterations > HASH_ITERATIONS_LIMIT ||
            !name_is_valid(user->name, MN_NAME_USER - 1U, 0)) {
            return 0;
        }
    }
    for (unsigned int i = 0; i < MN_MAX_FILES; i++) {
        struct file_record *file = &files[i];

        if (!file->used) {
            continue;
        }
        if (file->length > MN_FILE_DATA || file->owner >= MN_MAX_USERS ||
            !accounts->users[file->owner].used ||
            !name_is_valid(file->name, MN_NAME_FILE - 1U, 1)) {
            return 0;
        }
    }
    return 1;
}

static int load_copy(unsigned int lba, unsigned int *sequence)
{
    if (bios_disk_io(0x42, lba, STORE_SECTORS, STORE_BUFFER) != 0) {
        return MN_ERR_IO;
    }
    if (!store_is_sane()) {
        return MN_ERR_INVALID;
    }
    *sequence = accounts->sequence;
    return MN_OK;
}

int store_load(void)
{
    unsigned int sequence_a = 0;
    unsigned int sequence_b = 0;
    int status_a = load_copy(STORE_LBA_A, &sequence_a);
    unsigned char blank_a = status_a == MN_ERR_INVALID && accounts->magic == 0;
    int status_b = load_copy(STORE_LBA_B, &sequence_b);
    unsigned char blank_b = status_b == MN_ERR_INVALID && accounts->magic == 0;

    persistent = 0;
    if (status_a == MN_OK && (status_b != MN_OK || sequence_a >= sequence_b)) {
        /* Le tampon contient la copie B: recharger la copie A retenue. */
        load_copy(STORE_LBA_A, &sequence_a);
        active_copy_lba = STORE_LBA_A;
        persistent = 1;
        return 0;
    }
    if (status_b == MN_OK) {
        active_copy_lba = STORE_LBA_B;
        persistent = 1;
        return 0;
    }
    mn_zero(accounts, STORE_SECTORS * 512U);
    active_copy_lba = STORE_LBA_B;
    if (status_a == MN_ERR_IO || status_b == MN_ERR_IO) {
        return -1;
    }
    persistent = 1;
    return blank_a && blank_b ? 1 : 2;
}

unsigned char store_persistent(void)
{
    return persistent;
}

static int last_save_status = MN_OK;

int store_last_status(void)
{
    return last_save_status;
}

static int store_save_inner(void);

static int store_save(void)
{
    last_save_status = store_save_inner();
    return last_save_status;
}

static int store_save_inner(void)
{
    unsigned int target = active_copy_lba == STORE_LBA_A ? STORE_LBA_B : STORE_LBA_A;

    accounts->magic = STORE_MAGIC;
    accounts->version = STORE_VERSION;
    accounts->sequence++;
    compute_checks();
    if (!persistent) {
        return MN_ERR_IO;
    }
    if (bios_disk_io(0x43, target, STORE_SECTORS, STORE_BUFFER) != 0) {
        return MN_ERR_IO;
    }
    active_copy_lba = target;
    return MN_OK;
}

unsigned char name_is_valid(const char *name, unsigned int maximum, unsigned char allow_dot)
{
    unsigned int length = 0;

    while (name[length] != '\0') {
        char c = name[length];

        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
              (allow_dot && c == '.'))) {
            return 0;
        }
        length++;
        if (length > maximum) {
            return 0;
        }
    }
    return length != 0;
}

static unsigned int string_length(const char *text)
{
    unsigned int length = 0;

    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static unsigned char strings_equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

int user_slot_count(void)
{
    return MN_MAX_USERS;
}

unsigned char user_slot_used(unsigned int slot)
{
    return slot < MN_MAX_USERS && accounts->users[slot].used;
}

const char *user_slot_name(unsigned int slot)
{
    return accounts->users[slot % MN_MAX_USERS].name;
}

unsigned char user_slot_role(unsigned int slot)
{
    return accounts->users[slot % MN_MAX_USERS].role;
}

int user_find(const char *name)
{
    for (unsigned int i = 0; i < MN_MAX_USERS; i++) {
        if (accounts->users[i].used && strings_equal(accounts->users[i].name, name)) {
            return (int)i;
        }
    }
    return MN_ERR_NOTFOUND;
}

static void set_password_fields(struct user_record *user, const char *password)
{
    random_bytes(user->salt, sizeof(user->salt));
    user->iterations = HASH_ITERATIONS;
    pbkdf2_sha256((const unsigned char *)password, string_length(password),
                  user->salt, sizeof(user->salt), user->iterations, user->hash);
}

int user_add(const char *name, const char *password, unsigned char role)
{
    unsigned int slot;
    struct user_record *user;

    if (!name_is_valid(name, MN_NAME_USER - 1U, 0) || string_length(password) < MN_PASSWORD_MIN ||
        string_length(password) > MN_PASSWORD_MAX || role > MN_ROLE_ADMIN) {
        return MN_ERR_INVALID;
    }
    if (user_find(name) >= 0) {
        return MN_ERR_EXISTS;
    }
    for (slot = 0; slot < MN_MAX_USERS && accounts->users[slot].used; slot++) {
    }
    if (slot == MN_MAX_USERS) {
        return MN_ERR_FULL;
    }
    user = &accounts->users[slot];
    mn_zero(user, sizeof(*user));
    for (unsigned int i = 0; name[i] != '\0'; i++) {
        user->name[i] = name[i];
    }
    user->role = slot == 0 ? MN_ROLE_ADMIN : role;
    set_password_fields(user, password);
    user->used = 1;
    store_save();
    return (int)slot;
}

int user_set_password(unsigned int slot, const char *password)
{
    if (!user_slot_used(slot) || string_length(password) < MN_PASSWORD_MIN ||
        string_length(password) > MN_PASSWORD_MAX) {
        return MN_ERR_INVALID;
    }
    set_password_fields(&accounts->users[slot], password);
    return store_save();
}

unsigned char user_check_password(unsigned int slot, const char *password)
{
    unsigned char hash[32];
    unsigned char ok;

    if (!user_slot_used(slot) || string_length(password) > MN_PASSWORD_MAX) {
        return 0;
    }
    pbkdf2_sha256((const unsigned char *)password, string_length(password),
                  accounts->users[slot].salt, sizeof(accounts->users[slot].salt),
                  accounts->users[slot].iterations, hash);
    ok = mn_equal(hash, accounts->users[slot].hash, 32);
    mn_zero(hash, sizeof(hash));
    return ok;
}

int user_remove(unsigned int slot)
{
    if (slot == 0 || !user_slot_used(slot)) {
        return MN_ERR_INVALID;
    }
    for (unsigned int i = 0; i < MN_MAX_FILES; i++) {
        if (files[i].used && files[i].owner == slot) {
            mn_zero(&files[i], sizeof(files[i]));
        }
    }
    mn_zero(&accounts->users[slot], sizeof(accounts->users[slot]));
    return store_save();
}

int file_find(const char *name)
{
    for (unsigned int i = 0; i < MN_MAX_FILES; i++) {
        if (files[i].used && strings_equal(files[i].name, name)) {
            return (int)i;
        }
    }
    return MN_ERR_NOTFOUND;
}

unsigned char file_slot_used(unsigned int slot)
{
    return slot < MN_MAX_FILES && files[slot].used;
}

const char *file_slot_name(unsigned int slot)
{
    return files[slot % MN_MAX_FILES].name;
}

unsigned char file_slot_owner(unsigned int slot)
{
    return files[slot % MN_MAX_FILES].owner;
}

unsigned char file_slot_shared(unsigned int slot)
{
    return files[slot % MN_MAX_FILES].shared;
}

unsigned int file_slot_length(unsigned int slot)
{
    return files[slot % MN_MAX_FILES].length;
}

const char *file_slot_data(unsigned int slot)
{
    return files[slot % MN_MAX_FILES].data;
}

/* Cree le fichier, ou le remplace s'il existe. Les droits sont verifies par l'appelant. */
int file_write(const char *name, const char *data, unsigned int length,
               unsigned char owner, unsigned char shared)
{
    int slot = file_find(name);
    struct file_record *file;

    if (!name_is_valid(name, MN_NAME_FILE - 1U, 1) || length > MN_FILE_DATA ||
        owner >= MN_MAX_USERS) {
        return MN_ERR_INVALID;
    }
    if (slot < 0) {
        for (slot = 0; slot < (int)MN_MAX_FILES && files[slot].used; slot++) {
        }
        if (slot == (int)MN_MAX_FILES) {
            return MN_ERR_FULL;
        }
    }
    file = &files[slot];
    mn_zero(file, sizeof(*file));
    for (unsigned int i = 0; name[i] != '\0'; i++) {
        file->name[i] = name[i];
    }
    mn_copy(file->data, data, length);
    file->length = (unsigned short)length;
    file->owner = owner;
    file->shared = shared ? 1 : 0;
    file->used = 1;
    store_save();
    return slot;
}

int file_remove(unsigned int slot)
{
    if (!file_slot_used(slot)) {
        return MN_ERR_NOTFOUND;
    }
    mn_zero(&files[slot], sizeof(files[slot]));
    return store_save();
}
