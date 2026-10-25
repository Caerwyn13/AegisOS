#include "auth.h"
#include "aegisfs.h"
#include "heap.h"
#include "pit.h"
#include "string.h"

#define AUTH_DB_NAME ".accounts"
#define AUTH_DB_MAGIC 0x41454749
#define AUTH_DB_VERSION 1
#define AUTH_SALT_SIZE 16
#define AUTH_HASH_SIZE 32
#define AUTH_HASH_ITERATIONS 10000

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint8_t root_salt[AUTH_SALT_SIZE];
    uint8_t root_hash[AUTH_HASH_SIZE];
    uint8_t user_salt[AUTH_SALT_SIZE];
    uint8_t user_hash[AUTH_HASH_SIZE];
} auth_database_t;

typedef struct {
    uint32_t state[8];
    uint64_t bit_length;
    uint8_t block[64];
    uint32_t block_length;
} sha256_context_t;

static auth_database_t auth_database;
static int database_loaded;
static int session_active;
static int session_root;
static char session_username[AUTH_USERNAME_MAX];

static const uint32_t sha256_constants[64] = {
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
    0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
    0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
    0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
    0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
    0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
    0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
    0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
    0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
    0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
    0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
    0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
    0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
    0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

static uint32_t rotate_right(uint32_t value, uint32_t amount) {
    return (value >> amount) | (value << (32 - amount));
}

static void sha256_transform(sha256_context_t *context,
                             const uint8_t block[64]) {
    uint32_t words[64];
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t offset = i * 4;
        words[i] = ((uint32_t)block[offset] << 24) |
                   ((uint32_t)block[offset + 1] << 16) |
                   ((uint32_t)block[offset + 2] << 8) |
                   block[offset + 3];
    }
    for (uint32_t i = 16; i < 64; i++) {
        uint32_t s0 = rotate_right(words[i - 15], 7) ^
                      rotate_right(words[i - 15], 18) ^
                      (words[i - 15] >> 3);
        uint32_t s1 = rotate_right(words[i - 2], 17) ^
                      rotate_right(words[i - 2], 19) ^
                      (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    uint32_t a = context->state[0];
    uint32_t b = context->state[1];
    uint32_t c = context->state[2];
    uint32_t d = context->state[3];
    uint32_t e = context->state[4];
    uint32_t f = context->state[5];
    uint32_t g = context->state[6];
    uint32_t h = context->state[7];

    for (uint32_t i = 0; i < 64; i++) {
        uint32_t upper1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                          rotate_right(e, 25);
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t first = h + upper1 + choice +
                         sha256_constants[i] + words[i];
        uint32_t upper0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                          rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t second = upper0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void sha256_init(sha256_context_t *context) {
    static const uint32_t initial_state[8] = {
        0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
        0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
    };
    memcpy(context->state, initial_state, sizeof(initial_state));
    context->bit_length = 0;
    context->block_length = 0;
}

static void sha256_update(sha256_context_t *context, const uint8_t *data,
                          uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        context->block[context->block_length++] = data[i];
        if (context->block_length == sizeof(context->block)) {
            sha256_transform(context, context->block);
            context->bit_length += 512;
            context->block_length = 0;
        }
    }
}

static void sha256_final(sha256_context_t *context, uint8_t digest[32]) {
    uint32_t index = context->block_length;
    context->block[index++] = 0x80;
    if (index > 56) {
        while (index < 64)
            context->block[index++] = 0;
        sha256_transform(context, context->block);
        index = 0;
    }
    while (index < 56)
        context->block[index++] = 0;

    uint64_t bit_length = context->bit_length +
                          (uint64_t)context->block_length * 8;
    for (uint32_t i = 0; i < 8; i++)
        context->block[63 - i] = (uint8_t)(bit_length >> (i * 8));
    sha256_transform(context, context->block);

    for (uint32_t i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)(context->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(context->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(context->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)context->state[i];
    }
}

static void sha256(const uint8_t *data, uint32_t length, uint8_t digest[32]) {
    sha256_context_t context;
    sha256_init(&context);
    sha256_update(&context, data, length);
    sha256_final(&context, digest);
}

static void hmac_sha256(const uint8_t *key, uint32_t key_length,
                        const uint8_t *message, uint32_t message_length,
                        uint8_t digest[32]) {
    uint8_t key_block[64];
    uint8_t inner_data[96];
    uint8_t outer_data[96];
    uint8_t key_hash[32];
    memset(key_block, 0, sizeof(key_block));
    if (key_length > sizeof(key_block)) {
        sha256(key, key_length, key_hash);
        memcpy(key_block, key_hash, sizeof(key_hash));
    } else {
        memcpy(key_block, key, key_length);
    }

    for (uint32_t i = 0; i < sizeof(key_block); i++) {
        inner_data[i] = key_block[i] ^ 0x36;
        outer_data[i] = key_block[i] ^ 0x5C;
    }
    memcpy(inner_data + sizeof(key_block), message, message_length);
    sha256(inner_data, sizeof(key_block) + message_length, key_hash);
    memcpy(outer_data + sizeof(key_block), key_hash, sizeof(key_hash));
    sha256(outer_data, sizeof(key_block) + sizeof(key_hash), digest);
}

static void derive_password_hash(const char *password,
                                const uint8_t salt[AUTH_SALT_SIZE],
                                uint8_t hash[AUTH_HASH_SIZE]) {
    uint8_t salt_block[AUTH_SALT_SIZE + 4];
    uint8_t current[AUTH_HASH_SIZE];
    uint8_t result[AUTH_HASH_SIZE];
    memcpy(salt_block, salt, AUTH_SALT_SIZE);
    salt_block[AUTH_SALT_SIZE] = 0;
    salt_block[AUTH_SALT_SIZE + 1] = 0;
    salt_block[AUTH_SALT_SIZE + 2] = 0;
    salt_block[AUTH_SALT_SIZE + 3] = 1;

    uint32_t password_length = strlen(password);
    hmac_sha256((const uint8_t *)password, password_length,
                salt_block, sizeof(salt_block), current);
    memcpy(result, current, sizeof(result));
    for (uint32_t iteration = 1; iteration < AUTH_HASH_ITERATIONS;
         iteration++) {
        hmac_sha256((const uint8_t *)password, password_length,
                    current, sizeof(current), current);
        for (uint32_t i = 0; i < sizeof(result); i++)
            result[i] ^= current[i];
    }
    memcpy(hash, result, sizeof(result));
    memset(current, 0, sizeof(current));
    memset(result, 0, sizeof(result));
}

static void make_salt(uint8_t salt[AUTH_SALT_SIZE], uint32_t account_index) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    uint32_t state = low ^ high ^ pit_ticks() ^ (account_index * 0x9E3779B9);
    for (uint32_t i = 0; i < AUTH_SALT_SIZE; i++) {
        state = state * 1664525 + 1013904223;
        salt[i] = (uint8_t)(state >> 16);
        if ((i & 3) == 3) {
            __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
            state ^= low ^ high ^ pit_ticks();
        }
    }
}

static int hashes_match(const uint8_t left[AUTH_HASH_SIZE],
                        const uint8_t right[AUTH_HASH_SIZE]) {
    uint8_t difference = 0;
    for (uint32_t i = 0; i < AUTH_HASH_SIZE; i++)
        difference |= left[i] ^ right[i];
    return difference == 0;
}

int auth_load(void) {
    database_loaded = 0;
    session_active = 0;
    if (!fs_exists(AUTH_DB_NAME))
        return 1;

    uint8_t *buffer = (uint8_t *)kmalloc(FS_MAX_SIZE);
    if (!buffer)
        return -1;
    uint32_t size = 0;
    int status = fs_read(AUTH_DB_NAME, buffer, &size);
    if (status == 0 && size == sizeof(auth_database_t)) {
        memcpy(&auth_database, buffer, sizeof(auth_database));
        if (auth_database.magic == AUTH_DB_MAGIC &&
            auth_database.version == AUTH_DB_VERSION)
            database_loaded = 1;
        else
            status = -1;
    } else {
        status = -1;
    }
    memset(buffer, 0, FS_MAX_SIZE);
    kfree(buffer);
    return database_loaded ? 0 : status;
}

int auth_create_accounts(const char *root_password,
                         const char *user_password) {
    if (!root_password || !user_password || fs_exists(AUTH_DB_NAME))
        return -1;
    uint32_t root_length = strlen(root_password);
    uint32_t user_length = strlen(user_password);
    if (root_length < AUTH_PASSWORD_MIN ||
        root_length >= AUTH_PASSWORD_MAX ||
        user_length < AUTH_PASSWORD_MIN ||
        user_length >= AUTH_PASSWORD_MAX)
        return -1;

    memset(&auth_database, 0, sizeof(auth_database));
    auth_database.magic = AUTH_DB_MAGIC;
    auth_database.version = AUTH_DB_VERSION;
    make_salt(auth_database.root_salt, 0);
    make_salt(auth_database.user_salt, 1);
    derive_password_hash(root_password, auth_database.root_salt,
                         auth_database.root_hash);
    derive_password_hash(user_password, auth_database.user_salt,
                         auth_database.user_hash);
    if (fs_write(AUTH_DB_NAME, (const uint8_t *)&auth_database,
                 sizeof(auth_database)) != 0) {
        memset(&auth_database, 0, sizeof(auth_database));
        return -1;
    }
    database_loaded = 1;
    return 0;
}

int auth_verify(const char *username, const char *password) {
    if (!database_loaded || !username || !password)
        return -1;

    const uint8_t *salt;
    const uint8_t *expected;
    int is_root;
    if (strcmp(username, "root") == 0) {
        salt = auth_database.root_salt;
        expected = auth_database.root_hash;
        is_root = 1;
    } else if (strcmp(username, "user") == 0) {
        salt = auth_database.user_salt;
        expected = auth_database.user_hash;
        is_root = 0;
    } else {
        return -1;
    }

    uint8_t actual[AUTH_HASH_SIZE];
    derive_password_hash(password, salt, actual);
    int match = hashes_match(actual, expected);
    memset(actual, 0, sizeof(actual));
    if (!match)
        return -1;
    auth_session_start(username, is_root);
    return is_root ? 1 : 0;
}

void auth_session_start(const char *username, int is_root) {
    if (!username)
        return;
    uint32_t length = strlen(username);
    if (!length || length >= sizeof(session_username))
        return;
    memcpy(session_username, username, length + 1);
    session_root = is_root != 0;
    session_active = 1;
}

void auth_session_logout(void) {
    memset(session_username, 0, sizeof(session_username));
    session_root = 0;
    session_active = 0;
}

int auth_is_logged_in(void) {
    return session_active;
}

int auth_is_root(void) {
    return session_active && session_root;
}

int auth_is_sensitive_file(const char *name) {
    return name && strcmp(name, AUTH_DB_NAME) == 0;
}

const char *auth_username(void) {
    return session_active ? session_username : "";
}
