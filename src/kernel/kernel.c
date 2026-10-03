#include "usb.h"
#include "crypto.h"
#include "persist.h"
#include "driver.h"
#include "compiler.h"

#define USB_MAX 16U

static struct usb_entry usb_entries[USB_MAX];
static unsigned int usb_count;
static unsigned int usb_controllers;
static unsigned int usb_status;
static unsigned int usb_page;
static unsigned char usb_scanned;

static unsigned char read_port(unsigned short port);
static unsigned int read_port_dword(unsigned short port);
static void write_port_dword(unsigned short port, unsigned int value);
static void disable_hardware_cursor(void);
static void write_line(volatile unsigned char *video, unsigned int row, const char *text);
static unsigned int pci_count_devices(void);
static unsigned char pci_has_device(unsigned short vendor, unsigned short device_id);
static void show_hardware(volatile unsigned char *video, unsigned int page);
static void show_firmware(volatile unsigned char *video);
static void show_usb(volatile unsigned char *video, unsigned int page);
static void run_report(volatile unsigned char *video);
static const char *usb_firmware_hint(const struct usb_entry *entry);
static const char *pci_class_name(unsigned char class_code, unsigned char subclass);
static const char *pci_driver_hint(unsigned short vendor, unsigned char class_code,
                                   unsigned char subclass, unsigned char prog_if);
static const char *pci_firmware_hint(unsigned short vendor, unsigned short device_id,
                                     unsigned char class_code, unsigned char subclass);
static unsigned int append_text(char *buffer, unsigned int position,
                                const char *text);
static unsigned int append_hex(char *buffer, unsigned int position,
                               unsigned int value, unsigned int digits);
static unsigned int append_decimal(char *buffer, unsigned int position,
                                   unsigned int value);

static volatile unsigned char *const screen = (volatile unsigned char *)0xb8000;
static unsigned int hardware_page;
static unsigned int output_row;
static int session_user = -1;
static unsigned int login_failures;

static const char keys_plain[0x3a] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
    [0x1e] = 'a', [0x1f] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2b] = '\\', [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c',
    [0x2f] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' '
};

static const char keys_shift[0x3a] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0a] = '(', [0x0b] = ')', [0x0c] = '_', [0x0d] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1a] = '{', [0x1b] = '}',
    [0x1e] = 'A', [0x1f] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2b] = '|', [0x2c] = 'Z', [0x2d] = 'X', [0x2e] = 'C',
    [0x2f] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?', [0x39] = ' '
};

static unsigned int text_length(const char *text)
{
    unsigned int length = 0;

    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static unsigned char text_equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static void clear_row(unsigned int row)
{
    for (unsigned int column = 0; column < 80; column++) {
        screen[(row * 80 + column) * 2] = ' ';
        screen[(row * 80 + column) * 2 + 1] = 0x07;
    }
}

static void clear_screen(void)
{
    for (unsigned int row = 0; row < 25; row++) {
        clear_row(row);
    }
    output_row = 2;
}

static void output_start(void)
{
    for (unsigned int row = 2; row < 24; row++) {
        clear_row(row);
    }
    output_row = 2;
}

static void output_line(const char *text)
{
    if (output_row < 24) {
        write_line(screen, output_row, text);
        output_row++;
    }
}

static void output_pair(const char *first, const char *second)
{
    char line[80];
    unsigned int position = append_text(line, 0, first);

    append_text(line, position, second);
    output_line(line);
}

static void wait_cycles(unsigned long long cycles)
{
    unsigned long long start;
    unsigned long long now;

    __asm__ volatile ("rdtsc" : "=A"(start));
    do {
        __asm__ volatile ("rdtsc" : "=A"(now));
    } while (now - start < cycles);
}

/* Lit une ligne au clavier PS/2. visible=0 affiche des '*' (mots de passe). */
static unsigned int read_line(unsigned int row, unsigned int start, char *buffer,
                              unsigned int maximum, unsigned char visible)
{
    unsigned int length = 0;
    unsigned int blink_counter = 0;
    unsigned char cursor_visible = 1;
    unsigned char shift = 0;
    unsigned char skip_next = 0;
    volatile unsigned int delay;

    buffer[0] = '\0';
    if (maximum > 79 - start) {
        maximum = 79 - start;
    }
    for (;;) {
        if (read_port(0x64) & 1) {
            unsigned char scancode = read_port(0x60);

            entropy_add_timestamp();
            if (skip_next) {
                skip_next = 0;
            } else if (scancode == 0xe0) {
                skip_next = 1;
            } else if (scancode == 0x2a || scancode == 0x36) {
                shift = 1;
            } else if (scancode == 0xaa || scancode == 0xb6) {
                shift = 0;
            } else if (!(scancode & 0x80)) {
                if (scancode == 0x1c) {
                    screen[(row * 80 + start + length) * 2] = ' ';
                    return length;
                }
                if (scancode == 0x0e && length > 0) {
                    length--;
                    buffer[length] = '\0';
                    screen[(row * 80 + start + length) * 2] = ' ';
                    screen[(row * 80 + start + length + 1) * 2] = ' ';
                } else if (scancode < 0x3a && length < maximum) {
                    char key = shift ? keys_shift[scancode] : keys_plain[scancode];

                    if (key != '\0') {
                        buffer[length] = key;
                        screen[(row * 80 + start + length) * 2] = visible ? key : '*';
                        screen[(row * 80 + start + length) * 2 + 1] = 0x0f;
                        length++;
                        buffer[length] = '\0';
                    }
                }
            }
        }

        for (delay = 0; delay < 10000; delay++) {
        }
        blink_counter++;
        if (blink_counter >= 100) {
            blink_counter = 0;
            cursor_visible = !cursor_visible;
            screen[(row * 80 + start + length) * 2] = cursor_visible ? '_' : ' ';
            screen[(row * 80 + start + length) * 2 + 1] = 0x0f;
        }
    }
}

static unsigned int ask(const char *label, char *buffer, unsigned int maximum,
                        unsigned char visible)
{
    unsigned int row = output_row < 24 ? output_row : 23;
    unsigned int start = text_length(label);

    write_line(screen, row, label);
    output_row = row + 1;
    return read_line(row, start, buffer, maximum, visible);
}

static void show_status_line(void)
{
    char line[80];
    unsigned int position = 0;

    clear_row(24);
    line[0] = '\0';
    if (session_user >= 0) {
        position = append_text(line, position, "Utilisateur: ");
        position = append_text(line, position, user_slot_name((unsigned int)session_user));
        position = append_text(line, position,
                               user_slot_role((unsigned int)session_user) == MN_ROLE_ADMIN ?
                               " (admin)" : " (standard)");
        position = append_text(line, position, " | ");
    }
    append_text(line, position, store_persistent() ? "Persistance: active" :
                                                     "Persistance: INDISPONIBLE");
    write_line(screen, 24, line);
}

static void show_title(void)
{
    write_line(screen, 0, "Bienvenue dans MiniNux");
}

static unsigned char is_admin(void)
{
    return session_user >= 0 && user_slot_role((unsigned int)session_user) == MN_ROLE_ADMIN;
}

static void warn_if_unsaved(void)
{
    if (store_last_status() != MN_OK || !store_persistent()) {
        output_line("ATTENTION: ecriture disque impossible, modification non persistante.");
    }
}

static const char *error_text(int status)
{
    switch (status) {
    case MN_ERR_EXISTS: return "existe deja";
    case MN_ERR_FULL: return "plus de place";
    case MN_ERR_NOTFOUND: return "introuvable";
    case MN_ERR_DENIED: return "acces refuse";
    case MN_ERR_IO: return "erreur disque";
    default: return "valeur invalide";
    }
}

/* Demande un nouveau mot de passe deux fois; retourne 1 si accepte. */
static unsigned char ask_new_password(char *password)
{
    char again[MN_PASSWORD_MAX + 2];
    unsigned char ok = 0;
    unsigned int length;

    length = ask("Nouveau mot de passe (8 a 40 car.): ", password, MN_PASSWORD_MAX + 1, 0);
    if (length < MN_PASSWORD_MIN || length > MN_PASSWORD_MAX) {
        output_line("Mot de passe refuse: 8 caracteres minimum, 40 maximum.");
    } else {
        ask("Confirmer le mot de passe: ", again, MN_PASSWORD_MAX + 1, 0);
        if (text_equal(password, again)) {
            ok = 1;
        } else {
            output_line("Les mots de passe different.");
        }
    }
    mn_zero(again, sizeof(again));
    return ok;
}

static void first_boot_setup(void)
{
    char name[MN_NAME_USER + 1];
    char password[MN_PASSWORD_MAX + 2];

    for (;;) {
        clear_screen();
        show_title();
        show_status_line();
        write_line(screen, 2, "Premier demarrage: creation du compte administrateur.");
        output_row = 4;
        ask("Nom (a-z 0-9 - _ , 15 max): ", name, MN_NAME_USER - 1U, 1);
        if (!name_is_valid(name, MN_NAME_USER - 1U, 0)) {
            output_line("Nom invalide.");
            wait_cycles(2000000000ULL);
            continue;
        }
        if (ask_new_password(password)) {
            int slot = user_add(name, password, MN_ROLE_ADMIN);

            mn_zero(password, sizeof(password));
            if (slot >= 0) {
                if (store_last_status() != MN_OK || !store_persistent()) {
                    output_line("ATTENTION: disque non inscriptible, compte non persistant.");
                    wait_cycles(4000000000ULL);
                }
                return;
            }
            output_pair("Echec: ", error_text(slot));
        }
        mn_zero(password, sizeof(password));
        wait_cycles(2000000000ULL);
    }
}

static int login_screen(void)
{
    char name[MN_NAME_USER + 1];
    char password[MN_PASSWORD_MAX + 2];
    static const unsigned char dummy_salt[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    unsigned char dummy_hash[32];

    for (;;) {
        int slot;
        unsigned char granted = 0;

        clear_screen();
        show_title();
        show_status_line();
        write_line(screen, 2, "Connexion requise.");
        output_row = 4;
        ask("Utilisateur: ", name, MN_NAME_USER - 1U, 1);
        ask("Mot de passe: ", password, MN_PASSWORD_MAX + 1, 0);
        slot = user_find(name);
        if (slot >= 0) {
            granted = user_check_password((unsigned int)slot, password);
        } else {
            /* Meme cout de calcul pour ne pas reveler si le compte existe. */
            pbkdf2_sha256((const unsigned char *)password, text_length(password),
                          dummy_salt, sizeof(dummy_salt), 5000, dummy_hash);
        }
        mn_zero(password, sizeof(password));
        mn_zero(dummy_hash, sizeof(dummy_hash));
        if (granted) {
            login_failures = 0;
            return slot;
        }
        login_failures++;
        output_line("Identifiants invalides.");
        output_line("Patientez...");
        wait_cycles(1500000000ULL * (login_failures < 10 ? login_failures : 10));
    }
}

static char *next_word(char **cursor)
{
    char *position = *cursor;
    char *start;

    while (*position == ' ') {
        position++;
    }
    if (*position == '\0') {
        *cursor = position;
        return 0;
    }
    start = position;
    while (*position != '\0' && *position != ' ') {
        position++;
    }
    if (*position != '\0') {
        *position = '\0';
        position++;
    }
    *cursor = position;
    return start;
}

static unsigned char can_read_file(unsigned int slot)
{
    return is_admin() || file_slot_shared(slot) || file_slot_owner(slot) == (unsigned int)session_user;
}

static unsigned char can_modify_file(unsigned int slot)
{
    return is_admin() || file_slot_owner(slot) == (unsigned int)session_user;
}

static unsigned char text_has_slash(const char *text)
{
    for (; *text != '\0'; text++) {
        if (*text == '/') {
            return 1;
        }
    }
    return 0;
}

/* Arborescence: les commandes sont rangees par niveau de privilege. */
enum { DIR_USR_SYS, DIR_ROOT_SYS };

static const char *const directories[] = {
    "/usr", "/usr/sys", "/usr/sys/bin", "/usr/pentest", "/usr/pentest/bin",
    "/root", "/root/sys", "/root/sys/bin", "/root/pentest", "/root/pentest/bin",
    "/etc", "/home", "/tmp"
};

static const char *const bin_paths[] = {"/usr/sys/bin", "/root/sys/bin"};

static const struct {
    const char *name;
    unsigned char dir;
} commands[] = {
    {"help", DIR_USR_SYS}, {"clear", DIR_USR_SYS}, {"whoami", DIR_USR_SYS},
    {"users", DIR_USR_SYS}, {"logout", DIR_USR_SYS}, {"passwd", DIR_USR_SYS},
    {"ls", DIR_USR_SYS}, {"which", DIR_USR_SYS}, {"tinyc", DIR_USR_SYS},
    {"files", DIR_USR_SYS}, {"cat", DIR_USR_SYS}, {"write", DIR_USR_SYS},
    {"append", DIR_USR_SYS}, {"rm", DIR_USR_SYS}, {"hardware", DIR_USR_SYS},
    {"hardware-next", DIR_USR_SYS}, {"hardware-prev", DIR_USR_SYS},
    {"firmware", DIR_USR_SYS}, {"drivers", DIR_USR_SYS},
    {"useradd", DIR_ROOT_SYS}, {"userdel", DIR_ROOT_SYS}, {"usb", DIR_ROOT_SYS},
    {"usb-next", DIR_ROOT_SYS}, {"usb-prev", DIR_ROOT_SYS}, {"report", DIR_ROOT_SYS}
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))
#define DIRECTORY_COUNT (sizeof(directories) / sizeof(directories[0]))

static int command_find(const char *name)
{
    for (unsigned int index = 0; index < COMMAND_COUNT; index++) {
        if (text_equal(commands[index].name, name)) {
            return (int)index;
        }
    }
    return -1;
}

static unsigned char path_is_root_only(const char *path)
{
    return path[0] == '/' && path[1] == 'r' && path[2] == 'o' && path[3] == 'o' &&
           path[4] == 't' && (path[5] == '\0' || path[5] == '/');
}

static unsigned char is_direct_child(const char *parent, const char *path)
{
    unsigned int length = text_length(parent);

    if (length == 1) {
        return path[0] == '/' && path[1] != '\0' && text_length(path) > 1 &&
               !text_has_slash(path + 1);
    }
    for (unsigned int index = 0; index < length; index++) {
        if (path[index] != parent[index]) {
            return 0;
        }
    }
    return path[length] == '/' && path[length + 1] != '\0' &&
           !text_has_slash(path + length + 1);
}

static void command_ls(char *cursor)
{
    char *path = next_word(&cursor);
    unsigned char known = 0;
    unsigned char listed = 0;

    output_start();
    if (path == 0) {
        path = "/";
    }
    if (text_length(path) > 1 && path[text_length(path) - 1U] == '/') {
        output_line("Chemin invalide (pas de / final).");
        return;
    }
    if (path_is_root_only(path) && !is_admin()) {
        output_line("Acces refuse.");
        return;
    }
    if (text_equal(path, "/")) {
        output_line("usr  root  etc  home  tmp");
        return;
    }
    for (unsigned int index = 0; index < DIRECTORY_COUNT; index++) {
        if (text_equal(directories[index], path)) {
            known = 1;
        } else if (is_direct_child(path, directories[index])) {
            output_line(directories[index]);
            listed = 1;
        }
    }
    for (unsigned int index = 0; index < sizeof(bin_paths) / sizeof(bin_paths[0]); index++) {
        if (text_equal(bin_paths[index], path)) {
            for (unsigned int cmd = 0; cmd < COMMAND_COUNT; cmd++) {
                if (commands[cmd].dir == index) {
                    output_line(commands[cmd].name);
                }
            }
            listed = 1;
        }
    }
    if (!known) {
        output_line("Chemin introuvable.");
    } else if (!listed) {
        output_line("(vide)");
    }
}

static void command_which(char *cursor)
{
    char *name = next_word(&cursor);
    int index = name == 0 ? -1 : command_find(name);

    output_start();
    if (index < 0 || (commands[index].dir == DIR_ROOT_SYS && !is_admin())) {
        output_line("Commande introuvable.");
    } else {
        char line[48];
        unsigned int position = append_text(line, 0, bin_paths[commands[index].dir]);

        position = append_text(line, position, "/");
        append_text(line, position, commands[index].name);
        output_line(line);
    }
}

static void command_drivers(void)
{
    static const char *const state_text[] = {"charge", "SANS PILOTE", "ECHEC"};

    output_start();
    for (unsigned int index = 0; index < driver_count(); index++) {
        const struct driver_record *record = driver_get(index);
        char line[80];
        unsigned int position = append_text(line, 0, record->name);

        while (position < 12) {
            line[position++] = ' ';
        }
        if (record->vendor != 0) {
            position = append_hex(line, position, record->vendor, 4);
            line[position++] = ':';
            position = append_hex(line, position, record->device, 4);
            position = append_text(line, position, "  ");
        } else {
            position = append_text(line, position, "(noyau)    ");
        }
        append_text(line, position, state_text[record->state]);
        output_line(line);
    }
}

static void command_tinyc(char *cursor)
{
    char *name = next_word(&cursor);
    char source[MN_FILE_DATA + 1];
    unsigned char bytecode[256];
    unsigned int source_length;
    unsigned int bytecode_length;
    int result;
    int slot;

    output_start();
    if (name == 0) {
        output_line("Usage: tinyc <fichier.c>");
        return;
    }
    slot = file_find(name);
    if (slot < 0 || !can_read_file((unsigned int)slot)) {
        output_line("Fichier introuvable.");
        return;
    }
    source_length = file_slot_length((unsigned int)slot);
    if (source_length > MN_FILE_DATA) {
        output_line("Source trop grande.");
        return;
    }
    mn_copy(source, file_slot_data((unsigned int)slot), source_length);
    source[source_length] = '\0';
    bytecode_length = tinyc_compile_source(source, bytecode, sizeof(bytecode));
    mn_zero(source, sizeof(source));
    if (bytecode_length == 0) {
        output_line("Erreur TinyC: source invalide ou bytecode trop grand.");
    } else if (tinyc_execute(bytecode, bytecode_length, &result) != 0) {
        output_line("Erreur TinyC: execution invalide (division par zero?).");
    } else {
        char output[32];
        char digits[12];
        unsigned int count = 0;
        unsigned int position = 0;
        unsigned int value;

        if (result < 0) {
            output[position++] = '-';
            value = 0U - (unsigned int)result;
        } else {
            value = (unsigned int)result;
        }
        do {
            digits[count++] = (char)('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (count != 0) {
            output[position++] = digits[--count];
        }
        output[position] = '\0';
        output_pair("Programme compile et execute. main() = ", output);
    }
    mn_zero(bytecode, sizeof(bytecode));
}

static void command_help(void)
{
    output_line("Commandes: help clear whoami users logout passwd [nom] ls [chemin] which");
    output_line("Materiel: drivers hardware hardware-next hardware-prev firmware");
    output_line("Fichiers (persistants): files cat <nom> rm <nom>");
    output_line("  write [-s] <nom> <texte>   (-s: lisible par tous)   append <nom> <texte>");
    output_line("TinyC: tinyc <fichier.c> (int main() { return expression; })");
    output_line("/root/sys/bin (admin): useradd <nom> [admin] userdel <nom> usb usb-next usb-prev report");
}

static void command_users(void)
{
    output_start();
    for (unsigned int slot = 0; slot < MN_MAX_USERS; slot++) {
        if (user_slot_used(slot)) {
            output_pair(user_slot_name(slot), user_slot_role(slot) == MN_ROLE_ADMIN ?
                                               "  (admin)" : "  (standard)");
        }
    }
}

static void command_passwd(char *cursor)
{
    char *target_name = next_word(&cursor);
    char current[MN_PASSWORD_MAX + 2];
    char fresh[MN_PASSWORD_MAX + 2];
    int slot = session_user;

    output_start();
    if (target_name != 0 && !text_equal(target_name, user_slot_name((unsigned int)session_user))) {
        if (!is_admin()) {
            output_line("Acces refuse: seul un administrateur change le mot de passe d'un autre.");
            return;
        }
        slot = user_find(target_name);
        if (slot < 0) {
            output_line("Utilisateur introuvable.");
            return;
        }
    } else {
        ask("Mot de passe actuel: ", current, MN_PASSWORD_MAX + 1, 0);
        if (!user_check_password((unsigned int)slot, current)) {
            mn_zero(current, sizeof(current));
            output_line("Mot de passe actuel incorrect.");
            wait_cycles(1500000000ULL);
            return;
        }
        mn_zero(current, sizeof(current));
    }
    if (ask_new_password(fresh)) {
        user_set_password((unsigned int)slot, fresh);
        output_line("Mot de passe modifie.");
        warn_if_unsaved();
    }
    mn_zero(fresh, sizeof(fresh));
}

static void command_useradd(char *cursor)
{
    char *name = next_word(&cursor);
    char *role = next_word(&cursor);
    char password[MN_PASSWORD_MAX + 2];
    unsigned char role_value = MN_ROLE_USER;

    output_start();
    if (name == 0 || !name_is_valid(name, MN_NAME_USER - 1U, 0)) {
        output_line("Usage: useradd <nom> [admin]  (nom: a-z 0-9 - _ , 15 max)");
        return;
    }
    if (role != 0) {
        if (!text_equal(role, "admin")) {
            output_line("Usage: useradd <nom> [admin]");
            return;
        }
        role_value = MN_ROLE_ADMIN;
    }
    if (user_find(name) >= 0) {
        output_line("Cet utilisateur existe deja.");
        return;
    }
    if (ask_new_password(password)) {
        int slot = user_add(name, password, role_value);

        if (slot >= 0) {
            output_line("Utilisateur cree.");
            warn_if_unsaved();
        } else {
            output_pair("Echec: ", error_text(slot));
        }
    }
    mn_zero(password, sizeof(password));
}

static void command_userdel(char *cursor)
{
    char *name = next_word(&cursor);
    int slot;

    output_start();
    if (name == 0) {
        output_line("Usage: userdel <nom>");
        return;
    }
    slot = user_find(name);
    if (slot < 0) {
        output_line("Utilisateur introuvable.");
    } else if (slot == 0 || slot == session_user) {
        output_line("Impossible de supprimer le compte principal ou votre propre compte.");
    } else {
        user_remove((unsigned int)slot);
        output_line("Utilisateur et ses fichiers supprimes.");
        warn_if_unsaved();
    }
}

static void command_files(void)
{
    unsigned int shown = 0;

    output_start();
    for (unsigned int slot = 0; slot < MN_MAX_FILES; slot++) {
        char line[80];
        unsigned int position = 0;

        if (!file_slot_used(slot) || !can_read_file(slot)) {
            continue;
        }
        position = append_text(line, position, file_slot_name(slot));
        while (position < 26) {
            line[position++] = ' ';
        }
        line[position] = '\0';
        position = append_decimal(line, position, file_slot_length(slot));
        position = append_text(line, position, " o  ");
        position = append_text(line, position, user_slot_name(file_slot_owner(slot)));
        append_text(line, position, file_slot_shared(slot) ? "  [partage]" : "");
        output_line(line);
        shown++;
    }
    if (shown == 0) {
        output_line("Aucun fichier.");
    }
}

static void command_cat(char *cursor)
{
    char *name = next_word(&cursor);
    int slot;

    output_start();
    slot = name == 0 ? MN_ERR_INVALID : file_find(name);
    if (slot < 0 || !can_read_file((unsigned int)slot)) {
        output_line("Fichier introuvable.");
        return;
    }
    {
        char line[80];
        const char *data = file_slot_data((unsigned int)slot);
        unsigned int length = file_slot_length((unsigned int)slot);
        unsigned int position = 0;

        for (unsigned int index = 0; index < length; index++) {
            if (data[index] == '\n' || position == 79) {
                line[position] = '\0';
                output_line(line);
                position = 0;
                if (data[index] == '\n') {
                    continue;
                }
            }
            line[position++] = (data[index] >= 32 && data[index] < 127) ? data[index] : '?';
        }
        line[position] = '\0';
        output_line(line);
    }
}

static void command_write(char *cursor, unsigned char append)
{
    char *name = next_word(&cursor);
    unsigned char shared = 0;
    char *text;
    int slot;
    unsigned int old_length = 0;
    unsigned int text_size;
    char data[MN_FILE_DATA];
    unsigned char owner = (unsigned char)session_user;

    output_start();
    if (!append && name != 0 && text_equal(name, "-s")) {
        shared = 1;
        name = next_word(&cursor);
    }
    while (*cursor == ' ') {
        cursor++;
    }
    text = cursor;
    if (name == 0 || *text == '\0') {
        output_line(append ? "Usage: append <nom> <texte>" : "Usage: write [-s] <nom> <texte>");
        return;
    }
    if (!name_is_valid(name, MN_NAME_FILE - 1U, 1)) {
        output_line("Nom de fichier invalide (a-z 0-9 . - _ , 23 max).");
        return;
    }
    slot = file_find(name);
    if (slot >= 0) {
        if (!can_modify_file((unsigned int)slot)) {
            output_line("Acces refuse.");
            return;
        }
        owner = file_slot_owner((unsigned int)slot);
        if (append) {
            old_length = file_slot_length((unsigned int)slot);
            mn_copy(data, file_slot_data((unsigned int)slot), old_length);
            shared = file_slot_shared((unsigned int)slot);
        } else if (!shared) {
            shared = file_slot_shared((unsigned int)slot);
        }
    } else if (append) {
        output_line("Fichier introuvable.");
        return;
    }
    text_size = text_length(text);
    if (old_length + text_size + (append ? 1U : 0U) > MN_FILE_DATA) {
        output_line("Fichier trop grand (224 octets maximum).");
        return;
    }
    if (append) {
        data[old_length++] = '\n';
    }
    mn_copy(data + old_length, text, text_size);
    slot = file_write(name, data, old_length + text_size, owner, shared);
    if (slot < 0) {
        output_pair("Echec: ", error_text(slot));
    } else {
        output_line("Fichier enregistre.");
        warn_if_unsaved();
    }
}

static void command_rm(char *cursor)
{
    char *name = next_word(&cursor);
    int slot;

    output_start();
    slot = name == 0 ? MN_ERR_INVALID : file_find(name);
    if (slot < 0 || !can_read_file((unsigned int)slot)) {
        output_line("Fichier introuvable.");
    } else if (!can_modify_file((unsigned int)slot)) {
        output_line("Acces refuse.");
    } else {
        file_remove((unsigned int)slot);
        output_line("Fichier supprime.");
        warn_if_unsaved();
    }
}

/* Retourne 1 pour terminer la session. */
static unsigned char run_command(char *line)
{
    char *cursor = line;
    char *word = next_word(&cursor);

    if (word == 0) {
        return 0;
    }
    if (text_equal(word, "logout")) {
        return 1;
    } else if (text_equal(word, "help")) {
        output_start();
        command_help();
    } else if (text_equal(word, "clear")) {
        output_start();
    } else if (text_equal(word, "whoami")) {
        output_start();
        output_pair(user_slot_name((unsigned int)session_user),
                    is_admin() ? "  (admin)" : "  (standard)");
    } else if (text_equal(word, "users")) {
        command_users();
    } else if (text_equal(word, "passwd")) {
        command_passwd(cursor);
    } else if (text_equal(word, "files")) {
        command_files();
    } else if (text_equal(word, "cat")) {
        command_cat(cursor);
    } else if (text_equal(word, "write")) {
        command_write(cursor, 0);
    } else if (text_equal(word, "append")) {
        command_write(cursor, 1);
    } else if (text_equal(word, "rm")) {
        command_rm(cursor);
    } else if (text_equal(word, "ls")) {
        command_ls(cursor);
    } else if (text_equal(word, "which")) {
        command_which(cursor);
    } else if (text_equal(word, "tinyc")) {
        command_tinyc(cursor);
    } else if (text_equal(word, "hardware")) {
        hardware_page = 0;
        show_hardware(screen, hardware_page);
    } else if (text_equal(word, "hardware-next")) {
        hardware_page++;
        show_hardware(screen, hardware_page);
    } else if (text_equal(word, "hardware-prev")) {
        if (hardware_page > 0) {
            hardware_page--;
        }
        show_hardware(screen, hardware_page);
    } else if (text_equal(word, "drivers")) {
        command_drivers();
    } else if (text_equal(word, "firmware")) {
        show_firmware(screen);
    } else if (text_equal(word, "useradd") || text_equal(word, "userdel") ||
               text_equal(word, "usb") || text_equal(word, "usb-next") ||
               text_equal(word, "usb-prev") || text_equal(word, "report")) {
        if (!is_admin()) {
            output_start();
            output_line("Acces refuse: binaire de /root/sys/bin (administrateur).");
        } else if (text_equal(word, "useradd")) {
            command_useradd(cursor);
        } else if (text_equal(word, "userdel")) {
            command_userdel(cursor);
        } else if (text_equal(word, "usb")) {
            usb_page = 0;
            usb_scanned = 1;
            usb_count = usb_scan(usb_entries, USB_MAX, &usb_controllers, &usb_status);
            show_usb(screen, usb_page);
        } else if (text_equal(word, "usb-next")) {
            usb_page++;
            show_usb(screen, usb_page);
        } else if (text_equal(word, "usb-prev")) {
            if (usb_page > 0) {
                usb_page--;
            }
            show_usb(screen, usb_page);
        } else {
            run_report(screen);
        }
    } else {
        output_start();
        output_line("Commande inconnue (help pour la liste)");
    }
    return 0;
}

__attribute__((section(".text.startup"))) void kernel_main(void)
{
    char line[80];
    char prompt[MN_NAME_USER + 16];
    int store_state;

    disable_hardware_cursor();
    clear_screen();
    show_title();
    store_state = store_load();
    if (store_state == -1) {
        write_line(screen, 2, "Disque illisible: aucune persistance (comptes en memoire).");
        wait_cycles(3000000000ULL);
    } else if (store_state == 2) {
        write_line(screen, 2, "Stockage corrompu: nouvelle configuration requise.");
        wait_cycles(3000000000ULL);
    }
    drivers_load(store_state != -1, read_port(0x64) != 0xff);
    if (!user_slot_used(0)) {
        first_boot_setup();
    }

    for (;;) {
        unsigned int prompt_length;
        unsigned char logout = 0;

        session_user = -1;
        session_user = login_screen();
        clear_screen();
        show_title();
        show_status_line();
        output_start();
        write_line(screen, 2, "Session ouverte. Tapez help pour la liste des commandes.");
        prompt_length = append_text(prompt, 0, user_slot_name((unsigned int)session_user));
        prompt_length = append_text(prompt, prompt_length, "@mininux> ");

        while (!logout) {
            clear_row(1);
            write_line(screen, 1, prompt);
            read_line(1, prompt_length, line, 60, 1);
            logout = run_command(line);
            mn_zero(line, sizeof(line));
            show_status_line();
        }
        session_user = -1;
        clear_screen();
    }
}

static unsigned char read_port(unsigned short port)
{
    unsigned char value;

    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static unsigned int read_port_dword(unsigned short port)
{
    unsigned int value;

    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void write_port_dword(unsigned short port, unsigned int value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static void disable_hardware_cursor(void)
{
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x0a), "Nd"((unsigned short)0x3d4));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x20), "Nd"((unsigned short)0x3d5));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x0b), "Nd"((unsigned short)0x3d4));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x00), "Nd"((unsigned short)0x3d5));
}

static void write_line(volatile unsigned char *video, unsigned int row, const char *text)
{
    for (unsigned int index = 0; text[index] != '\0' && index < 80; index++) {
        unsigned int offset = (row * 80 + index) * 2;
        video[offset] = text[index];
        video[offset + 1] = 0x0f;
    }
}

void pci_write_config(unsigned char bus, unsigned char device,
                      unsigned char function, unsigned char offset,
                      unsigned int value)
{
    unsigned int address = 0x80000000U |
                           ((unsigned int)bus << 16) |
                           ((unsigned int)device << 11) |
                           ((unsigned int)function << 8) |
                           ((unsigned int)offset & 0xfcU);

    write_port_dword(0x0cf8, address);
    write_port_dword(0x0cfc, value);
}

unsigned int pci_read_config(unsigned char bus, unsigned char device,
                             unsigned char function, unsigned char offset)
{
    unsigned int address = 0x80000000U |
                           ((unsigned int)bus << 16) |
                           ((unsigned int)device << 11) |
                           ((unsigned int)function << 8) |
                           ((unsigned int)offset & 0xfcU);

    write_port_dword(0x0cf8, address);
    return read_port_dword(0x0cfc);
}

static unsigned int pci_count_devices(void)
{
    unsigned int count = 0;

    for (unsigned int bus = 0; bus < 256; bus++) {
        for (unsigned int device = 0; device < 32; device++) {
            unsigned int id = pci_read_config((unsigned char)bus,
                                              (unsigned char)device, 0, 0);
            unsigned short vendor = (unsigned short)(id & 0xffffU);
            unsigned int header = pci_read_config((unsigned char)bus,
                                                  (unsigned char)device, 0, 0x0c);
            unsigned int function_count = (header & 0x00800000U) != 0 ? 8 : 1;

            if (vendor == 0xffffU) {
                continue;
            }

            count++;
            for (unsigned int function = 1; function < function_count; function++) {
                id = pci_read_config((unsigned char)bus, (unsigned char)device,
                                     (unsigned char)function, 0);
                if ((id & 0xffffU) != 0xffffU) {
                    count++;
                }
            }
        }
    }
    return count;
}

static unsigned char pci_has_device(unsigned short vendor, unsigned short device_id)
{
    for (unsigned int bus = 0; bus < 256; bus++) {
        for (unsigned int device = 0; device < 32; device++) {
            unsigned int id = pci_read_config((unsigned char)bus,
                                              (unsigned char)device, 0, 0);
            unsigned short found_vendor = (unsigned short)(id & 0xffffU);
            unsigned int header = pci_read_config((unsigned char)bus,
                                                  (unsigned char)device, 0, 0x0c);
            unsigned int function_count = (header & 0x00800000U) != 0 ? 8 : 1;

            for (unsigned int function = 0; function < function_count; function++) {
                if (function != 0) {
                    id = pci_read_config((unsigned char)bus, (unsigned char)device,
                                         (unsigned char)function, 0);
                    found_vendor = (unsigned short)(id & 0xffffU);
                }
                if (found_vendor == vendor &&
                    (unsigned short)(id >> 16) == device_id) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void show_firmware(volatile unsigned char *video)
{
    unsigned char broadcom_wifi = pci_has_device(0x14e4, 0x43a0);
    unsigned char intel_graphics = pci_has_device(0x8086, 0x1626);

    for (unsigned int row = 2; row <= 23; row++) {
        for (unsigned int column = 0; column < 80; column++) {
            unsigned int offset = (row * 80 + column) * 2;
            video[offset] = ' ';
            video[offset + 1] = 0x07;
        }
    }

    write_line(video, 2, "Firmware - profil MacBook Air 13 pouces, 2015");
    if (broadcom_wifi && intel_graphics) {
        write_line(video, 4, "PCI confirme: BCM4360 14e4:43a0 + Intel 8086:1626");
    write_line(video, 6, "Wi-Fi: pilote wl proprietaire; blob inclus dans le pilote");
    write_line(video, 8, "GPU HD 6000: i915; DMC depend de la version du noyau");
    write_line(video, 10, "CPU: microcode Intel; Wi-Fi: regulatory.db");
    write_line(video, 12, "Bluetooth attendu: Apple USB 05ac:828f (a confirmer)");
    write_line(video, 14, "Hub attendu: Broadcom 0a5c:4500 (a confirmer)");
    write_line(video, 16, "Clavier/trackpad: 05ac:0291, hid_apple/bcm5974");
    write_line(video, 18, "Audio: snd_hda_intel/cirrus; pas de blob confirme");
    write_line(video, 20, "Fichier Bluetooth .hcd: revision exacte requise");
    write_line(video, 22, usb_scanned ? "USB scanne: voir la commande usb" : "USB non scanne: lancez usb (liste incomplete)");
    } else {
        write_line(video, 4, "Profil Air 2015 non confirme par les ID PCI.");
        write_line(video, 6, "BCM4360 attendu: 14e4:43a0");
        write_line(video, 8, "Intel HD 6000 attendue: 8086:1626");
        write_line(video, 10, "Lancez hardware pour relever les ID PCI detectes.");
        write_line(video, 12, "USB/Bluetooth necessite encore un inventaire USB.");
    }
}

static const char *pci_class_name(unsigned char class_code, unsigned char subclass)
{
    if (class_code == 0x01) {
        if (subclass == 0x00) {
            return "SCSI";
        }
        if (subclass == 0x06) {
            return "SATA";
        }
        if (subclass == 0x08) {
            return "NVMe";
        }
        if (subclass == 0x01) {
            return "IDE";
        }
        return "stockage";
    }
    if (class_code == 0x02) {
        if (subclass == 0x00) {
            return "Ethernet";
        }
        return "reseau autre";
    }
    if (class_code == 0x03) {
        return subclass == 0x00 ? "VGA" : "graphique";
    }
    if (class_code == 0x04) {
        return subclass == 0x03 ? "audio HD" : "audio/media";
    }
    if (class_code == 0x0c && subclass == 0x03) {
        return "USB";
    }
    if (class_code == 0x06) {
        return subclass == 0x04 ? "pont PCI" : "pont/chipset";
    }
    return "autre";
}

static const char *pci_driver_hint(unsigned short vendor, unsigned char class_code,
                                   unsigned char subclass, unsigned char prog_if)
{
    if (class_code == 0x01) {
        if (subclass == 0x06 && prog_if == 0x01) {
            return "ahci";
        }
        if (subclass == 0x08) {
            return "nvme";
        }
        if (subclass == 0x01) {
            return "IDE (a confirmer)";
        }
        return "stockage: ID requis";
    } else if (class_code == 0x02) {
        if (subclass == 0x80) {
            if (vendor == 0x8086) {
                return "candidat: iwlwifi";
            }
            if (vendor == 0x14e4) {
                return "candidat: Broadcom";
            }
            if (vendor == 0x168c) {
                return "candidat: ath9k/ath10k";
            }
            if (vendor == 0x10ec) {
                return "candidat: Realtek Wi-Fi";
            }
            return "reseau: identifier par ID";
        }
        if (subclass == 0x00 && vendor == 0x8086) {
            return "candidat: e1000e/igb";
        }
        if (subclass == 0x00 && vendor == 0x10ec) {
            return "candidat: r8169";
        }
        if (subclass == 0x00 && vendor == 0x14e4) {
            return "candidat: tg3/bnx2";
        }
        return "reseau: identifier par ID";
    } else if (class_code == 0x03) {
        if (vendor == 0x8086) {
            return "candidat: i915";
        }
        if (vendor == 0x1002) {
            return "candidat: amdgpu";
        }
        if (vendor == 0x10de) {
            return "candidat: nouveau";
        }
        return "GPU: identifier par ID";
    } else if (class_code == 0x04) {
        return "candidat: snd_hda_intel";
    } else if (class_code == 0x0c && subclass == 0x03) {
        if (prog_if == 0x30) {
            return "candidat: xHCI";
        }
        if (prog_if == 0x20) {
            return "candidat: EHCI";
        }
        if (prog_if == 0x10) {
            return "candidat: OHCI";
        }
        if (prog_if == 0x00) {
            return "candidat: UHCI";
        }
        return "USB: interface a identifier";
    }
    return "pilote: ID exact requis";
}

static const char *pci_firmware_hint(unsigned short vendor, unsigned short device_id,
                                     unsigned char class_code, unsigned char subclass)
{
    if (class_code == 0x02 && subclass == 0x80) {
        if (vendor == 0x14e4 && device_id == 0x43a0) {
            return "wl: blob integre au pilote";
        }
        if (vendor == 0x8086) {
            return "iwlwifi .ucode? (ID exact)";
        }
        if (vendor == 0x14e4) {
            return "Broadcom? fichier selon ID";
        }
        if (vendor == 0x168c) {
            return "ath firmware? (ID exact)";
        }
        if (vendor == 0x10ec) {
            return "Realtek? firmware selon ID";
        }
        return "inconnu (ID exact requis)";
    }
    if (class_code == 0x03 &&
        (vendor == 0x1002 || vendor == 0x10de || vendor == 0x8086)) {
        return "GPU: depend du modele/OS";
    }
    return "indetermine par PCI seul";
}

static unsigned int append_text(char *buffer, unsigned int position,
                                const char *text)
{
    while (text[0] != '\0' && position < 79) {
        buffer[position] = text[0];
        position++;
        text++;
    }
    buffer[position] = '\0';
    return position;
}

static unsigned int append_hex(char *buffer, unsigned int position,
                               unsigned int value, unsigned int digits)
{
    static const char hex_digits[] = "0123456789abcdef";

    for (unsigned int digit = digits; digit > 0 && position < 79; digit--) {
        unsigned int shift = (digit - 1) * 4;
        buffer[position] = hex_digits[(value >> shift) & 0x0fU];
        position++;
    }
    buffer[position] = '\0';
    return position;
}

static unsigned int append_decimal(char *buffer, unsigned int position,
                                   unsigned int value)
{
    char digits[10];
    unsigned int digit_count = 0;

    do {
        digits[digit_count] = (char)('0' + value % 10);
        digit_count++;
        value /= 10;
    } while (value != 0 && digit_count < sizeof(digits));

    while (digit_count > 0 && position < 79) {
        digit_count--;
        buffer[position] = digits[digit_count];
        position++;
    }
    buffer[position] = '\0';
    return position;
}

static void show_hardware(volatile unsigned char *video, unsigned int page)
{
    const unsigned int devices_per_page = 5;
    unsigned int total = pci_count_devices();
    unsigned int page_count = (total + devices_per_page - 1) / devices_per_page;
    unsigned int displayed = 0;
    unsigned int device_index = 0;

    for (unsigned int row = 2; row <= 23; row++) {
        for (unsigned int column = 0; column < 80; column++) {
            unsigned int offset = (row * 80 + column) * 2;
            video[offset] = ' ';
            video[offset + 1] = 0x07;
        }
    }

    if (page_count == 0) {
        page_count = 1;
    }
    if (page >= page_count) {
        page = page_count - 1;
    }

    write_line(video, 2, "Materiel PCI - IDs pour verifier pilotes et firmwares");

    for (unsigned int bus = 0; bus < 256; bus++) {
        for (unsigned int device = 0; device < 32; device++) {
            unsigned int id = pci_read_config((unsigned char)bus,
                                              (unsigned char)device, 0, 0);
            unsigned short vendor = (unsigned short)(id & 0xffffU);
            unsigned int header = pci_read_config((unsigned char)bus,
                                                  (unsigned char)device, 0, 0x0c);
            unsigned int function_count = (header & 0x00800000U) != 0 ? 8 : 1;

            if (vendor == 0xffffU) {
                continue;
            }

            for (unsigned int function = 0; function < function_count; function++) {
                if (function != 0) {
                    id = pci_read_config((unsigned char)bus, (unsigned char)device,
                                         (unsigned char)function, 0);
                    vendor = (unsigned short)(id & 0xffffU);
                    if (vendor == 0xffffU) {
                        continue;
                    }
                }

                if (device_index >= page * devices_per_page &&
                    displayed < devices_per_page) {
                    unsigned short device_id = (unsigned short)(id >> 16);
                    unsigned int function_header =
                        pci_read_config((unsigned char)bus, (unsigned char)device,
                                       (unsigned char)function, 0x0c);
                    unsigned int class_info = pci_read_config((unsigned char)bus,
                                                              (unsigned char)device,
                                                              (unsigned char)function,
                                                              0x08);
                    unsigned char class_code = (unsigned char)(class_info >> 24);
                    unsigned char subclass = (unsigned char)(class_info >> 16);
                    unsigned char header_type =
                        (unsigned char)((function_header >> 16) & 0x7fU);
                    char line[80] = {0};
                    unsigned int position = 0;
                    unsigned int subsystem_ids = pci_read_config((unsigned char)bus,
                                                                 (unsigned char)device,
                                                                 (unsigned char)function,
                                                                 header_type == 0x02
                                                                     ? 0x40 : 0x2c);
                    unsigned short subsystem_vendor =
                        (unsigned short)(subsystem_ids & 0xffffU);
                    unsigned short subsystem_device =
                        (unsigned short)(subsystem_ids >> 16);
                    unsigned char revision = (unsigned char)class_info;
                    unsigned char prog_if = (unsigned char)(class_info >> 8);
                    unsigned int row = 3 + displayed * 4;

                    position = append_text(line, position, "PCI ");
                    position = append_hex(line, position, bus, 2);
                    position = append_text(line, position, ":");
                    position = append_hex(line, position, device, 2);
                    position = append_text(line, position, ".");
                    position = append_hex(line, position, function, 1);
                    position = append_text(line, position, " ");
                    position = append_hex(line, position, vendor, 4);
                    position = append_text(line, position, ":");
                    position = append_hex(line, position, device_id, 4);
                    position = append_text(line, position, " rev ");
                    position = append_hex(line, position, revision, 2);
                    write_line(video, row, line);

                    position = 0;
                    line[0] = '\0';
                    position = append_text(line, position, " Subsystem ");
                    position = append_hex(line, position, subsystem_vendor, 4);
                    position = append_text(line, position, ":");
                    position = append_hex(line, position, subsystem_device, 4);
                    write_line(video, row + 1, line);

                    position = 0;
                    line[0] = '\0';
                    position = append_text(line, position, " Classe ");
                    position = append_hex(line, position, class_code, 2);
                    position = append_text(line, position, ":");
                    position = append_hex(line, position, subclass, 2);
                    position = append_text(line, position, ":");
                    position = append_hex(line, position, prog_if, 2);
                    position = append_text(line, position, " ");
                    position = append_text(line, position,
                                            pci_class_name(class_code, subclass));
                    write_line(video, row + 2, line);

                    position = 0;
                    line[0] = '\0';
                    position = append_text(line, position, " Pilote?: ");
                    if (vendor == 0x14e4 && device_id == 0x43a0 &&
                        class_code == 0x02 && subclass == 0x80) {
                        position = append_text(line, position, "wl proprietaire");
                    } else {
                        position = append_text(line, position,
                                                pci_driver_hint(vendor, class_code,
                                                                subclass, prog_if));
                    }
                    position = append_text(line, position, " FW?: ");
                    position = append_text(line, position,
                                            pci_firmware_hint(vendor, device_id,
                                                              class_code, subclass));
                    write_line(video, row + 3, line);
                    displayed++;
                }
                device_index++;
            }
        }
    }

    if (total == 0) {
        write_line(video, 4, "Aucun peripherique PCI detecte.");
    } else if (page_count > 1) {
        char summary[80] = {0};
        unsigned int position = 0;

        position = append_text(summary, position, "Detectes: ");
        position = append_decimal(summary, position, total);
        position = append_text(summary, position, " | page ");
        position = append_decimal(summary, position, page + 1);
        position = append_text(summary, position, "/");
        position = append_decimal(summary, position, page_count);
        position = append_text(summary, position, " | hardware-next/prev");
        write_line(video, 23, summary);
    } else {
        char summary[80] = {0};
        unsigned int position = append_text(summary, 0, "Detectes: ");

        position = append_decimal(summary, position, total);
        append_text(summary, position, " | FW a confirmer avec le modele exact");
        write_line(video, 23, summary);
    }
}
static const char *usb_firmware_hint(const struct usb_entry *entry)
{
    if (entry->vendor == 0x05ac && entry->product == 0x828f) {
        return "Bluetooth Broadcom; .hcd a confirmer";
    }
    if (entry->vendor == 0x0a5c && entry->product == 0x4500) {
        return "hub interne, pas de FW";
    }
    if (entry->vendor == 0x05ac && entry->product == 0x0291) {
        return "clavier/trackpad, pas de FW";
    }
    if (entry->device_class == 9) {
        return "hub, pas de FW";
    }
    if (entry->device_class == 3) {
        return "HID, pas de FW";
    }
    if (entry->device_class == 0xe0) {
        return "Bluetooth/sans fil: FW selon fabricant";
    }
    return "FW inconnu";
}

static void show_usb(volatile unsigned char *video, unsigned int page)
{
    unsigned int pages = (usb_count + 4U) / 5U;
    unsigned int first;
    unsigned int row = 3;
    char line[80];
    unsigned int position;

    for (unsigned int clear_row = 2; clear_row <= 23; clear_row++) {
        for (unsigned int column = 0; column < 80; column++) {
            video[(clear_row * 80 + column) * 2] = ' ';
            video[(clear_row * 80 + column) * 2 + 1] = 0x07;
        }
    }
    write_line(video, 2, "USB (xHCI) - peripheriques");

    if (usb_controllers == 0) {
        write_line(video, 4, "Aucun controleur xHCI detecte (classe PCI 0c:03:30).");
        return;
    }
    if (usb_status != 0) {
        write_line(video, 21, (usb_status & USB_STATUS_BAR_UNUSABLE) ?
                   "Attention: BAR xHCI inutilisable" : "Attention: init xHCI echouee");
    }
    if (usb_count == 0) {
        write_line(video, 4, "Aucun peripherique USB enumere.");
        return;
    }
    if (page >= pages) {
        page = pages - 1U;
        usb_page = page;
    }
    first = page * 5U;

    for (unsigned int index = first; index < usb_count && index < first + 5U; index++) {
        const struct usb_entry *entry = &usb_entries[index];

        position = 0;
        line[0] = '\0';
        position = append_text(line, position, "Port ");
        position = append_decimal(line, position, entry->root_port);
        position = append_text(line, position, " route ");
        position = append_hex(line, position, entry->route, 5);
        position = append_text(line, position, " ");
        if (entry->flags & USB_FLAG_FAILED) {
            position = append_text(line, position, "lecture echouee");
        } else {
            position = append_hex(line, position, entry->vendor, 4);
            position = append_text(line, position, ":");
            position = append_hex(line, position, entry->product, 4);
            position = append_text(line, position, " classe ");
            position = append_hex(line, position, entry->device_class, 2);
            position = append_text(line, position, entry->speed >= 4 ? " SS" :
                                   (entry->speed == 3 ? " HS" : " FS/LS"));
        }
        write_line(video, row, line);

        position = 0;
        line[0] = '\0';
        position = append_text(line, position, "  FW?: ");
        position = append_text(line, position,
                               (entry->flags & USB_FLAG_HUB_SKIPPED) ? "hub non parcouru" :
                               usb_firmware_hint(entry));
        write_line(video, row + 1, line);
        row += 3;
    }

    position = 0;
    line[0] = '\0';
    position = append_text(line, position, "Peripheriques: ");
    position = append_decimal(line, position, usb_count);
    position = append_text(line, position, " | page ");
    position = append_decimal(line, position, page + 1);
    position = append_text(line, position, "/");
    position = append_decimal(line, position, pages);
    position = append_text(line, position, " | usb-next/prev");
    write_line(video, 23, line);
}

#define REPORT_BUFFER ((volatile char *)0x20000)
#define REPORT_SIZE 65536U
#define REPORT_LBA 64U
#define REPORT_SECTORS 128U

static unsigned int report_length;

static void report_text(const char *text)
{
    for (unsigned int index = 0; text[index] != '\0' && report_length < REPORT_SIZE - 8U; index++) {
        REPORT_BUFFER[report_length++] = text[index];
    }
}

static void report_screen(const volatile unsigned char *video)
{
    for (unsigned int row = 2; row <= 23; row++) {
        unsigned int last = 0;

        for (unsigned int column = 0; column < 80; column++) {
            if (video[(row * 80 + column) * 2] != ' ') {
                last = column + 1;
            }
        }
        if (last == 0) {
            continue;
        }
        for (unsigned int column = 0; column < last && report_length < REPORT_SIZE - 8U; column++) {
            REPORT_BUFFER[report_length++] = (char)video[(row * 80 + column) * 2];
        }
        report_text("\n");
    }
}

/* Retourne le code BIOS (0 = succes). */
static unsigned int report_flush(void)
{
    unsigned int status = 0;

    for (unsigned int chunk = 0; chunk < REPORT_SECTORS / 16U && status == 0; chunk++) {
        status = bios_disk_io(0x43, REPORT_LBA + chunk * 16U, 16U, 0x20000U + chunk * 0x2000U);
    }
    return status;
}

static void report_status(volatile unsigned char *video, unsigned int row,
                          const char *label, unsigned int status)
{
    char line[80];
    unsigned int position = 0;

    line[0] = '\0';
    position = append_text(line, position, label);
    if (status == 0) {
        append_text(line, position, "OK");
    } else {
        position = append_text(line, position, "echec BIOS 0x");
        append_hex(line, position, status, 2);
    }
    write_line(video, row, line);
}

static void run_report(volatile unsigned char *video)
{
    unsigned int pages = (pci_count_devices() + 4U) / 5U;
    unsigned int first_status;

    for (unsigned int index = 0; index < REPORT_SIZE; index++) {
        REPORT_BUFFER[index] = '\0';
    }
    report_length = 0;
    report_text("MININUX RAPPORT MATERIEL\n\n");

    for (unsigned int page = 0; page < (pages == 0 ? 1U : pages); page++) {
        show_hardware(video, page);
        report_screen(video);
        report_text("\n");
    }
    show_firmware(video);
    report_screen(video);
    report_text("\n[phase PCI/firmware terminee]\n");

    first_status = report_flush();
    write_line(video, 2, "Rapport: phase PCI ecrite, scan USB en cours...");
    report_status(video, 3, "Ecriture phase 1: ", first_status);

    usb_page = 0;
    usb_scanned = 1;
    usb_count = usb_scan(usb_entries, USB_MAX, &usb_controllers, &usb_status);
    pages = (usb_count + 4U) / 5U;
    for (unsigned int page = 0; page < (pages == 0 ? 1U : pages); page++) {
        show_usb(video, page);
        report_screen(video);
        report_text("\n");
    }
    report_text("[FIN DU RAPPORT]\n");

    for (unsigned int row = 2; row <= 23; row++) {
        for (unsigned int column = 0; column < 80; column++) {
            video[(row * 80 + column) * 2] = ' ';
        }
    }
    write_line(video, 2, "Rapport termine (secteurs 64-191 de la cle).");
    report_status(video, 3, "Ecriture phase 1 (PCI): ", first_status);
    report_status(video, 4, "Ecriture finale (USB): ", report_flush());
    write_line(video, 6, "Sur Fedora: make read-report DEV=/dev/sdX");
}
