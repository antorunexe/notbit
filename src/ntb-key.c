/*
 * Notbit - A Bitmessage client
 * Copyright (C) 2013  Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <string.h>
#include <openssl/obj_mac.h>
#include <assert.h>

#include "ntb-key.h"
#include "ntb-util.h"
#include "ntb-buffer.h"
#include "ntb-proto.h"

static void
generate_tag(struct ntb_key *key)
{
        struct ntb_buffer buffer;
        uint8_t hash1[SHA512_DIGEST_LENGTH];
        uint8_t hash2[SHA512_DIGEST_LENGTH];
        SHA512_CTX sha_ctx;

        ntb_buffer_init(&buffer);
        ntb_proto_add_var_int(&buffer, key->version);
        ntb_proto_add_var_int(&buffer, key->stream);

        SHA512_Init(&sha_ctx);
        SHA512_Update(&sha_ctx, buffer.data, buffer.length);

        ntb_buffer_destroy(&buffer);

        SHA512_Update(&sha_ctx, key->ripe, RIPEMD160_DIGEST_LENGTH);
        SHA512_Final(hash1, &sha_ctx);

        SHA512_Init(&sha_ctx);
        SHA512_Update(&sha_ctx, hash1, SHA512_DIGEST_LENGTH);
        SHA512_Final(hash2, &sha_ctx);

        memcpy(key->tag_private_key, hash2, NTB_KEY_PRIVATE_SIZE);
        memcpy(key->tag, hash2 + NTB_KEY_PRIVATE_SIZE, NTB_KEY_TAG_SIZE);
}

static EC_KEY *
create_ec_key(const uint8_t *private_key,
              const uint8_t *public_key)
{
        EC_KEY *key;
        BIGNUM bn;
        BIGNUM *bn_ret;
        EC_POINT *point;
        const EC_GROUP *group;
        int int_ret;

        key = EC_KEY_new_by_curve_name(NID_secp256k1);
        assert(key);

        group = EC_KEY_get0_group(key);

        BN_init(&bn);

        bn_ret = BN_bin2bn(private_key, NTB_KEY_PRIVATE_SIZE, &bn);
        assert(bn_ret);

        int_ret = EC_KEY_set_private_key(key, &bn);
        assert(int_ret);

        BN_free(&bn);

        point = EC_POINT_new(group);
        assert(point);

        int_ret = EC_POINT_oct2point(group,
                                     point,
                                     public_key,
                                     NTB_KEY_PUBLIC_SIZE,
                                     NULL /* bignum context */);
        assert(int_ret);

        int_ret = EC_KEY_set_public_key(key, point);
        assert(int_ret);

        EC_POINT_free(point);

        return key;
}

struct ntb_key *
ntb_key_new(const char *label,
            const uint8_t *ripe,
            uint64_t version,
            uint64_t stream,
            const uint8_t *private_signing_key,
            const uint8_t *public_signing_key,
            const uint8_t *private_encryption_key,
            const uint8_t *public_encryption_key)
{
        struct ntb_key *key = ntb_alloc(sizeof *key);

        ntb_ref_count_init(&key->ref_count);

        key->label = ntb_strdup(label);
        key->version = version;
        key->stream = stream;
        key->nonce_trials_per_byte = NTB_PROTO_MIN_NONCE_TRIALS_PER_BYTE;
        key->payload_length_extra_bytes = NTB_PROTO_MIN_EXTRA_BYTES;
        key->last_pubkey_send_time = 0;
        key->enabled = true;
        key->decoy = false;

        memcpy(key->ripe, ripe, RIPEMD160_DIGEST_LENGTH);

        key->signing_key = create_ec_key(private_signing_key,
                                         public_signing_key);
        key->encryption_key = create_ec_key(private_encryption_key,
                                            public_encryption_key);

        generate_tag(key);

        return key;
}

struct ntb_key *
ntb_key_copy(struct ntb_key *key)
{
        key = ntb_memdup(key, sizeof *key);

        ntb_ref_count_init(&key->ref_count);

        key->label = ntb_strdup(key->label);

        key->signing_key = EC_KEY_dup(key->signing_key);
        assert(key->signing_key);
        key->encryption_key = EC_KEY_dup(key->encryption_key);
        assert(key->encryption_key);

        return key;
}

struct ntb_key *
ntb_key_ref(struct ntb_key *key)
{
        ntb_ref_count_ref(&key->ref_count);

        return key;
}

void
ntb_key_unref(struct ntb_key *key)
{
        if (ntb_ref_count_unref(&key->ref_count) <= 1) {
                EC_KEY_free(key->signing_key);
                EC_KEY_free(key->encryption_key);
                ntb_ref_count_destroy(&key->ref_count);
                ntb_free(key->label);
                ntb_free(key);
        }
}