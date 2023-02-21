/**
 * \file oid.c
 *
 * \brief Object Identifier (OID) database
 *
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "common.h"

#if defined(MBEDTLS_OID_C)

#include "mbedtls/oid.h"
#include "mbedtls/rsa.h"
#include "mbedtls/error.h"
#include "mbedtls/pk.h"

#include "mbedtls/legacy_or_psa.h"

#include <stdio.h>
#include <string.h>

#include "mbedtls/platform.h"

/*
 * Macro to automatically add the size of #define'd OIDs
 */
#define ADD_LEN(s)      s, MBEDTLS_OID_SIZE(s)

/*
 * Macro to generate mbedtls_oid_descriptor_t
 */
#if !defined(MBEDTLS_X509_REMOVE_INFO)
#define OID_DESCRIPTOR(s, name, description)  { ADD_LEN(s), name, description }
#define NULL_OID_DESCRIPTOR                   { NULL, 0, NULL, NULL }
#else
#define OID_DESCRIPTOR(s, name, description)  { ADD_LEN(s) }
#define NULL_OID_DESCRIPTOR                   { NULL, 0 }
#endif

/*
 * Macro to generate an internal function for oid_XXX_from_asn1() (used by
 * the other functions)
 */
#define FN_OID_TYPED_FROM_ASN1(TYPE_T, NAME, LIST)                    \
    static const TYPE_T *oid_ ## NAME ## _from_asn1(                   \
        const mbedtls_asn1_buf *oid)     \
    {                                                                   \
        const TYPE_T *p = (LIST);                                       \
        const mbedtls_oid_descriptor_t *cur =                           \
            (const mbedtls_oid_descriptor_t *) p;                       \
        if (p == NULL || oid == NULL) return NULL;                  \
        while (cur->asn1 != NULL) {                                    \
            if (cur->asn1_len == oid->len &&                            \
                memcmp(cur->asn1, oid->p, oid->len) == 0) {          \
                return p;                                            \
            }                                                           \
            p++;                                                        \
            cur = (const mbedtls_oid_descriptor_t *) p;                 \
        }                                                               \
        return NULL;                                                 \
    }

#if !defined(MBEDTLS_X509_REMOVE_INFO)
/*
 * Macro to generate a function for retrieving a single attribute from the
 * descriptor of an mbedtls_oid_descriptor_t wrapper.
 */
#define FN_OID_GET_DESCRIPTOR_ATTR1(FN_NAME, TYPE_T, TYPE_NAME, ATTR1_TYPE, ATTR1) \
    int FN_NAME(const mbedtls_asn1_buf *oid, ATTR1_TYPE * ATTR1)                  \
    {                                                                       \
        const TYPE_T *data = oid_ ## TYPE_NAME ## _from_asn1(oid);        \
        if (data == NULL) return MBEDTLS_ERR_OID_NOT_FOUND;            \
        *ATTR1 = data->descriptor.ATTR1;                                    \
        return 0;                                                        \
    }
#endif /* MBEDTLS_X509_REMOVE_INFO */

/*
 * Macro to generate a function for retrieving a single attribute from an
 * mbedtls_oid_descriptor_t wrapper.
 */
#define FN_OID_GET_ATTR1(FN_NAME, TYPE_T, TYPE_NAME, ATTR1_TYPE, ATTR1) \
    int FN_NAME(const mbedtls_asn1_buf *oid, ATTR1_TYPE * ATTR1)                  \
    {                                                                       \
        const TYPE_T *data = oid_ ## TYPE_NAME ## _from_asn1(oid);        \
        if (data == NULL) return MBEDTLS_ERR_OID_NOT_FOUND;            \
        *ATTR1 = data->ATTR1;                                               \
        return 0;                                                        \
    }

/*
 * Macro to generate a function for retrieving two attributes from an
 * mbedtls_oid_descriptor_t wrapper.
 */
#define FN_OID_GET_ATTR2(FN_NAME, TYPE_T, TYPE_NAME, ATTR1_TYPE, ATTR1,     \
                         ATTR2_TYPE, ATTR2)                                 \
    int FN_NAME(const mbedtls_asn1_buf *oid, ATTR1_TYPE * ATTR1,               \
                ATTR2_TYPE * ATTR2)              \
    {                                                                           \
        const TYPE_T *data = oid_ ## TYPE_NAME ## _from_asn1(oid);            \
        if (data == NULL) return MBEDTLS_ERR_OID_NOT_FOUND;                 \
        *(ATTR1) = data->ATTR1;                                                 \
        *(ATTR2) = data->ATTR2;                                                 \
        return 0;                                                            \
    }

/*
 * Macro to generate a function for retrieving the OID based on a single
 * attribute from a mbedtls_oid_descriptor_t wrapper.
 */
#define FN_OID_GET_OID_BY_ATTR1(FN_NAME, TYPE_T, LIST, ATTR1_TYPE, ATTR1)   \
    int FN_NAME(ATTR1_TYPE ATTR1, const char **oid, size_t *olen)             \
    {                                                                           \
        const TYPE_T *cur = (LIST);                                             \
        while (cur->descriptor.asn1 != NULL) {                                 \
            if (cur->ATTR1 == (ATTR1)) {                                       \
                *oid = cur->descriptor.asn1;                                    \
                *olen = cur->descriptor.asn1_len;                               \
                return 0;                                                    \
            }                                                                   \
            cur++;                                                              \
        }                                                                       \
        return MBEDTLS_ERR_OID_NOT_FOUND;                                    \
    }

/*
 * Macro to generate a function for retrieving the OID based on two
 * attributes from a mbedtls_oid_descriptor_t wrapper.
 */
#define FN_OID_GET_OID_BY_ATTR2(FN_NAME, TYPE_T, LIST, ATTR1_TYPE, ATTR1,   \
                                ATTR2_TYPE, ATTR2)                          \
    int FN_NAME(ATTR1_TYPE ATTR1, ATTR2_TYPE ATTR2, const char **oid,         \
                size_t *olen)                                                 \
    {                                                                           \
        const TYPE_T *cur = (LIST);                                             \
        while (cur->descriptor.asn1 != NULL) {                                 \
            if (cur->ATTR1 == (ATTR1) && cur->ATTR2 == (ATTR2)) {              \
                *oid = cur->descriptor.asn1;                                    \
                *olen = cur->descriptor.asn1_len;                               \
                return 0;                                                    \
            }                                                                   \
            cur++;                                                              \
        }                                                                       \
        return MBEDTLS_ERR_OID_NOT_FOUND;                                   \
    }

/*
 * For X520 attribute types
 */
typedef struct {
    mbedtls_oid_descriptor_t    descriptor;
    const char          *short_name;
} oid_x520_attr_t;

static const oid_x520_attr_t oid_x520_attr_type[] =
{
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_CN,          "id-at-commonName",               "Common Name"),
        "CN",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_COUNTRY,     "id-at-countryName",              "Country"),
        "C",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_LOCALITY,    "id-at-locality",                 "Locality"),
        "L",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_STATE,       "id-at-state",                    "State"),
        "ST",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_ORGANIZATION, "id-at-organizationName",
                       "Organization"),
        "O",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_ORG_UNIT,    "id-at-organizationalUnitName",   "Org Unit"),
        "OU",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_PKCS9_EMAIL,
                       "emailAddress",
                       "E-mail address"),
        "emailAddress",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_SERIAL_NUMBER,
                       "id-at-serialNumber",
                       "Serial number"),
        "serialNumber",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_POSTAL_ADDRESS,
                       "id-at-postalAddress",
                       "Postal address"),
        "postalAddress",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_POSTAL_CODE, "id-at-postalCode",               "Postal code"),
        "postalCode",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_SUR_NAME,    "id-at-surName",                  "Surname"),
        "SN",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_GIVEN_NAME,  "id-at-givenName",                "Given name"),
        "GN",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_INITIALS,    "id-at-initials",                 "Initials"),
        "initials",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_GENERATION_QUALIFIER,
                       "id-at-generationQualifier",
                       "Generation qualifier"),
        "generationQualifier",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_TITLE,       "id-at-title",                    "Title"),
        "title",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_DN_QUALIFIER,
                       "id-at-dnQualifier",
                       "Distinguished Name qualifier"),
        "dnQualifier",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_PSEUDONYM,   "id-at-pseudonym",                "Pseudonym"),
        "pseudonym",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_UID,            "id-uid",                         "User Id"),
        "uid",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_DOMAIN_COMPONENT,
                       "id-domainComponent",
                       "Domain component"),
        "DC",
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_AT_UNIQUE_IDENTIFIER,
                       "id-at-uniqueIdentifier",
                       "Unique Identifier"),
        "uniqueIdentifier",
    },
    {
        NULL_OID_DESCRIPTOR,
        NULL,
    }
};

FN_OID_TYPED_FROM_ASN1(oid_x520_attr_t, x520_attr, oid_x520_attr_type)
FN_OID_GET_ATTR1(mbedtls_oid_get_attr_short_name,
                 oid_x520_attr_t,
                 x520_attr,
                 const char *,
                 short_name)

/*
 * For X509 extensions
 */
typedef struct {
    mbedtls_oid_descriptor_t    descriptor;
    int                 ext_type;
} oid_x509_ext_t;

static const oid_x509_ext_t oid_x509_ext[] =
{
    {
        OID_DESCRIPTOR(MBEDTLS_OID_BASIC_CONSTRAINTS,
                       "id-ce-basicConstraints",
                       "Basic Constraints"),
        MBEDTLS_OID_X509_EXT_BASIC_CONSTRAINTS,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_KEY_USAGE,            "id-ce-keyUsage",            "Key Usage"),
        MBEDTLS_OID_X509_EXT_KEY_USAGE,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EXTENDED_KEY_USAGE,
                       "id-ce-extKeyUsage",
                       "Extended Key Usage"),
        MBEDTLS_OID_X509_EXT_EXTENDED_KEY_USAGE,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_SUBJECT_ALT_NAME,
                       "id-ce-subjectAltName",
                       "Subject Alt Name"),
        MBEDTLS_OID_X509_EXT_SUBJECT_ALT_NAME,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_NS_CERT_TYPE,
                       "id-netscape-certtype",
                       "Netscape Certificate Type"),
        MBEDTLS_OID_X509_EXT_NS_CERT_TYPE,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_CERTIFICATE_POLICIES,
                       "id-ce-certificatePolicies",
                       "Certificate Policies"),
        MBEDTLS_OID_X509_EXT_CERTIFICATE_POLICIES,
    },
    {
        NULL_OID_DESCRIPTOR,
        0,
    },
};

FN_OID_TYPED_FROM_ASN1(oid_x509_ext_t, x509_ext, oid_x509_ext)
FN_OID_GET_ATTR1(mbedtls_oid_get_x509_ext_type, oid_x509_ext_t, x509_ext, int, ext_type)

#if !defined(MBEDTLS_X509_REMOVE_INFO)
static const mbedtls_oid_descriptor_t oid_ext_key_usage[] =
{
    OID_DESCRIPTOR(MBEDTLS_OID_SERVER_AUTH,
                   "id-kp-serverAuth",
                   "TLS Web Server Authentication"),
    OID_DESCRIPTOR(MBEDTLS_OID_CLIENT_AUTH,
                   "id-kp-clientAuth",
                   "TLS Web Client Authentication"),
    OID_DESCRIPTOR(MBEDTLS_OID_CODE_SIGNING,     "id-kp-codeSigning",     "Code Signing"),
    OID_DESCRIPTOR(MBEDTLS_OID_EMAIL_PROTECTION, "id-kp-emailProtection", "E-mail Protection"),
    OID_DESCRIPTOR(MBEDTLS_OID_TIME_STAMPING,    "id-kp-timeStamping",    "Time Stamping"),
    OID_DESCRIPTOR(MBEDTLS_OID_OCSP_SIGNING,     "id-kp-OCSPSigning",     "OCSP Signing"),
    OID_DESCRIPTOR(MBEDTLS_OID_WISUN_FAN,
                   "id-kp-wisun-fan-device",
                   "Wi-SUN Alliance Field Area Network (FAN)"),
    NULL_OID_DESCRIPTOR,
};

FN_OID_TYPED_FROM_ASN1(mbedtls_oid_descriptor_t, ext_key_usage, oid_ext_key_usage)
FN_OID_GET_ATTR1(mbedtls_oid_get_extended_key_usage,
                 mbedtls_oid_descriptor_t,
                 ext_key_usage,
                 const char *,
                 description)

static const mbedtls_oid_descriptor_t oid_certificate_policies[] =
{
    OID_DESCRIPTOR(MBEDTLS_OID_ANY_POLICY,      "anyPolicy",       "Any Policy"),
    NULL_OID_DESCRIPTOR,
};

FN_OID_TYPED_FROM_ASN1(mbedtls_oid_descriptor_t, certificate_policies, oid_certificate_policies)
FN_OID_GET_ATTR1(mbedtls_oid_get_certificate_policies,
                 mbedtls_oid_descriptor_t,
                 certificate_policies,
                 const char *,
                 description)
#endif /* MBEDTLS_X509_REMOVE_INFO */

/*
 * For SignatureAlgorithmIdentifier
 */
typedef struct {
    mbedtls_oid_descriptor_t    descriptor;
    mbedtls_md_type_t           md_alg;
    mbedtls_pk_type_t           pk_alg;
} oid_sig_alg_t;

static const oid_sig_alg_t oid_sig_alg[] =
{
#if defined(MBEDTLS_RSA_C)
#if defined(MBEDTLS_HAS_ALG_MD5_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_PKCS1_MD5,        "md5WithRSAEncryption",     "RSA with MD5"),
        MBEDTLS_MD_MD5,      MBEDTLS_PK_RSA,
    },
#endif /* MBEDTLS_HAS_ALG_MD5_VIA_LOWLEVEL_OR_PSA */
#if defined(MBEDTLS_HAS_ALG_SHA_1_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_PKCS1_SHA1,       "sha-1WithRSAEncryption",   "RSA with SHA1"),
        MBEDTLS_MD_SHA1,     MBEDTLS_PK_RSA,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_1_VIA_LOWLEVEL_OR_PSA */
#if defined(MBEDTLS_HAS_ALG_SHA_224_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_PKCS1_SHA224,     "sha224WithRSAEncryption",
                       "RSA with SHA-224"),
        MBEDTLS_MD_SHA224,   MBEDTLS_PK_RSA,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_224_VIA_LOWLEVEL_OR_PSA */
#if defined(MBEDTLS_HAS_ALG_SHA_256_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_PKCS1_SHA256,     "sha256WithRSAEncryption",
                       "RSA with SHA-256"),
        MBEDTLS_MD_SHA256,   MBEDTLS_PK_RSA,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_256_VIA_LOWLEVEL_OR_PSA */
#if defined(MBEDTLS_HAS_ALG_SHA_384_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_PKCS1_SHA384,     "sha384WithRSAEncryption",
                       "RSA with SHA-384"),
        MBEDTLS_MD_SHA384,   MBEDTLS_PK_RSA,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_384_VIA_LOWLEVEL_OR_PSA */
#if defined(MBEDTLS_HAS_ALG_SHA_512_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_PKCS1_SHA512,     "sha512WithRSAEncryption",
                       "RSA with SHA-512"),
        MBEDTLS_MD_SHA512,   MBEDTLS_PK_RSA,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_512_VIA_LOWLEVEL_OR_PSA */
#if defined(MBEDTLS_HAS_ALG_SHA_1_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_RSA_SHA_OBS,      "sha-1WithRSAEncryption",   "RSA with SHA1"),
        MBEDTLS_MD_SHA1,     MBEDTLS_PK_RSA,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_1_VIA_LOWLEVEL_OR_PSA */
#endif /* MBEDTLS_RSA_C */
#if defined(MBEDTLS_PK_CAN_ECDSA_SOME)
#if defined(MBEDTLS_HAS_ALG_SHA_1_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_ECDSA_SHA1,       "ecdsa-with-SHA1",      "ECDSA with SHA1"),
        MBEDTLS_MD_SHA1,     MBEDTLS_PK_ECDSA,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_1_VIA_LOWLEVEL_OR_PSA */
#if defined(MBEDTLS_HAS_ALG_SHA_224_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_ECDSA_SHA224,     "ecdsa-with-SHA224",    "ECDSA with SHA224"),
        MBEDTLS_MD_SHA224,   MBEDTLS_PK_ECDSA,
    },
#endif
#if defined(MBEDTLS_HAS_ALG_SHA_256_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_ECDSA_SHA256,     "ecdsa-with-SHA256",    "ECDSA with SHA256"),
        MBEDTLS_MD_SHA256,   MBEDTLS_PK_ECDSA,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_256_VIA_LOWLEVEL_OR_PSA */
#if defined(MBEDTLS_HAS_ALG_SHA_384_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_ECDSA_SHA384,     "ecdsa-with-SHA384",    "ECDSA with SHA384"),
        MBEDTLS_MD_SHA384,   MBEDTLS_PK_ECDSA,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_384_VIA_LOWLEVEL_OR_PSA */
#if defined(MBEDTLS_HAS_ALG_SHA_512_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_ECDSA_SHA512,     "ecdsa-with-SHA512",    "ECDSA with SHA512"),
        MBEDTLS_MD_SHA512,   MBEDTLS_PK_ECDSA,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_512_VIA_LOWLEVEL_OR_PSA */
#endif /* MBEDTLS_PK_CAN_ECDSA_SOME */
#if defined(MBEDTLS_RSA_C)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_RSASSA_PSS,        "RSASSA-PSS",           "RSASSA-PSS"),
        MBEDTLS_MD_NONE,     MBEDTLS_PK_RSASSA_PSS,
    },
#endif /* MBEDTLS_RSA_C */
    {
        NULL_OID_DESCRIPTOR,
        MBEDTLS_MD_NONE, MBEDTLS_PK_NONE,
    },
};

FN_OID_TYPED_FROM_ASN1(oid_sig_alg_t, sig_alg, oid_sig_alg)

#if !defined(MBEDTLS_X509_REMOVE_INFO)
FN_OID_GET_DESCRIPTOR_ATTR1(mbedtls_oid_get_sig_alg_desc,
                            oid_sig_alg_t,
                            sig_alg,
                            const char *,
                            description)
#endif

FN_OID_GET_ATTR2(mbedtls_oid_get_sig_alg,
                 oid_sig_alg_t,
                 sig_alg,
                 mbedtls_md_type_t,
                 md_alg,
                 mbedtls_pk_type_t,
                 pk_alg)
FN_OID_GET_OID_BY_ATTR2(mbedtls_oid_get_oid_by_sig_alg,
                        oid_sig_alg_t,
                        oid_sig_alg,
                        mbedtls_pk_type_t,
                        pk_alg,
                        mbedtls_md_type_t,
                        md_alg)

/*
 * For PublicKeyInfo (PKCS1, RFC 5480)
 */
typedef struct {
    mbedtls_oid_descriptor_t    descriptor;
    mbedtls_pk_type_t           pk_alg;
} oid_pk_alg_t;

static const oid_pk_alg_t oid_pk_alg[] =
{
    {
        OID_DESCRIPTOR(MBEDTLS_OID_PKCS1_RSA,           "rsaEncryption",    "RSA"),
        MBEDTLS_PK_RSA,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_ALG_UNRESTRICTED, "id-ecPublicKey",   "Generic EC key"),
        MBEDTLS_PK_ECKEY,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_ALG_ECDH,         "id-ecDH",          "EC key for ECDH"),
        MBEDTLS_PK_ECKEY_DH,
    },
    {
        NULL_OID_DESCRIPTOR,
        MBEDTLS_PK_NONE,
    },
};

FN_OID_TYPED_FROM_ASN1(oid_pk_alg_t, pk_alg, oid_pk_alg)
FN_OID_GET_ATTR1(mbedtls_oid_get_pk_alg, oid_pk_alg_t, pk_alg, mbedtls_pk_type_t, pk_alg)
FN_OID_GET_OID_BY_ATTR1(mbedtls_oid_get_oid_by_pk_alg,
                        oid_pk_alg_t,
                        oid_pk_alg,
                        mbedtls_pk_type_t,
                        pk_alg)

#if defined(MBEDTLS_ECP_C)
/*
 * For namedCurve (RFC 5480)
 */
typedef struct {
    mbedtls_oid_descriptor_t    descriptor;
    mbedtls_ecp_group_id        grp_id;
} oid_ecp_grp_t;

static const oid_ecp_grp_t oid_ecp_grp[] =
{
#if defined(MBEDTLS_ECP_DP_SECP192R1_ENABLED)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_GRP_SECP192R1, "secp192r1",    "secp192r1"),
        MBEDTLS_ECP_DP_SECP192R1,
    },
#endif /* MBEDTLS_ECP_DP_SECP192R1_ENABLED */
#if defined(MBEDTLS_ECP_DP_SECP224R1_ENABLED)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_GRP_SECP224R1, "secp224r1",    "secp224r1"),
        MBEDTLS_ECP_DP_SECP224R1,
    },
#endif /* MBEDTLS_ECP_DP_SECP224R1_ENABLED */
#if defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_GRP_SECP256R1, "secp256r1",    "secp256r1"),
        MBEDTLS_ECP_DP_SECP256R1,
    },
#endif /* MBEDTLS_ECP_DP_SECP256R1_ENABLED */
#if defined(MBEDTLS_ECP_DP_SECP384R1_ENABLED)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_GRP_SECP384R1, "secp384r1",    "secp384r1"),
        MBEDTLS_ECP_DP_SECP384R1,
    },
#endif /* MBEDTLS_ECP_DP_SECP384R1_ENABLED */
#if defined(MBEDTLS_ECP_DP_SECP521R1_ENABLED)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_GRP_SECP521R1, "secp521r1",    "secp521r1"),
        MBEDTLS_ECP_DP_SECP521R1,
    },
#endif /* MBEDTLS_ECP_DP_SECP521R1_ENABLED */
#if defined(MBEDTLS_ECP_DP_SECP192K1_ENABLED)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_GRP_SECP192K1, "secp192k1",    "secp192k1"),
        MBEDTLS_ECP_DP_SECP192K1,
    },
#endif /* MBEDTLS_ECP_DP_SECP192K1_ENABLED */
#if defined(MBEDTLS_ECP_DP_SECP224K1_ENABLED)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_GRP_SECP224K1, "secp224k1",    "secp224k1"),
        MBEDTLS_ECP_DP_SECP224K1,
    },
#endif /* MBEDTLS_ECP_DP_SECP224K1_ENABLED */
#if defined(MBEDTLS_ECP_DP_SECP256K1_ENABLED)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_GRP_SECP256K1, "secp256k1",    "secp256k1"),
        MBEDTLS_ECP_DP_SECP256K1,
    },
#endif /* MBEDTLS_ECP_DP_SECP256K1_ENABLED */
#if defined(MBEDTLS_ECP_DP_BP256R1_ENABLED)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_GRP_BP256R1,   "brainpoolP256r1", "brainpool256r1"),
        MBEDTLS_ECP_DP_BP256R1,
    },
#endif /* MBEDTLS_ECP_DP_BP256R1_ENABLED */
#if defined(MBEDTLS_ECP_DP_BP384R1_ENABLED)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_GRP_BP384R1,   "brainpoolP384r1", "brainpool384r1"),
        MBEDTLS_ECP_DP_BP384R1,
    },
#endif /* MBEDTLS_ECP_DP_BP384R1_ENABLED */
#if defined(MBEDTLS_ECP_DP_BP512R1_ENABLED)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_EC_GRP_BP512R1,   "brainpoolP512r1", "brainpool512r1"),
        MBEDTLS_ECP_DP_BP512R1,
    },
#endif /* MBEDTLS_ECP_DP_BP512R1_ENABLED */
    {
        NULL_OID_DESCRIPTOR,
        MBEDTLS_ECP_DP_NONE,
    },
};

FN_OID_TYPED_FROM_ASN1(oid_ecp_grp_t, grp_id, oid_ecp_grp)
FN_OID_GET_ATTR1(mbedtls_oid_get_ec_grp, oid_ecp_grp_t, grp_id, mbedtls_ecp_group_id, grp_id)
FN_OID_GET_OID_BY_ATTR1(mbedtls_oid_get_oid_by_ec_grp,
                        oid_ecp_grp_t,
                        oid_ecp_grp,
                        mbedtls_ecp_group_id,
                        grp_id)
#endif /* MBEDTLS_ECP_C */

#if defined(MBEDTLS_CIPHER_C)
/*
 * For PKCS#5 PBES2 encryption algorithm
 */
typedef struct {
    mbedtls_oid_descriptor_t    descriptor;
    mbedtls_cipher_type_t       cipher_alg;
} oid_cipher_alg_t;

static const oid_cipher_alg_t oid_cipher_alg[] =
{
    {
        OID_DESCRIPTOR(MBEDTLS_OID_DES_CBC,              "desCBC",       "DES-CBC"),
        MBEDTLS_CIPHER_DES_CBC,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_DES_EDE3_CBC,         "des-ede3-cbc", "DES-EDE3-CBC"),
        MBEDTLS_CIPHER_DES_EDE3_CBC,
    },
    {
        NULL_OID_DESCRIPTOR,
        MBEDTLS_CIPHER_NONE,
    },
};

FN_OID_TYPED_FROM_ASN1(oid_cipher_alg_t, cipher_alg, oid_cipher_alg)
FN_OID_GET_ATTR1(mbedtls_oid_get_cipher_alg,
                 oid_cipher_alg_t,
                 cipher_alg,
                 mbedtls_cipher_type_t,
                 cipher_alg)
#endif /* MBEDTLS_CIPHER_C */

/*
 * For digestAlgorithm
 */
typedef struct {
    mbedtls_oid_descriptor_t    descriptor;
    mbedtls_md_type_t           md_alg;
} oid_md_alg_t;

static const oid_md_alg_t oid_md_alg[] =
{
#if defined(MBEDTLS_HAS_ALG_MD5_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_DIGEST_ALG_MD5,       "id-md5",       "MD5"),
        MBEDTLS_MD_MD5,
    },
#endif
#if defined(MBEDTLS_HAS_ALG_SHA_1_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_DIGEST_ALG_SHA1,      "id-sha1",      "SHA-1"),
        MBEDTLS_MD_SHA1,
    },
#endif
#if defined(MBEDTLS_HAS_ALG_SHA_224_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_DIGEST_ALG_SHA224,    "id-sha224",    "SHA-224"),
        MBEDTLS_MD_SHA224,
    },
#endif
#if defined(MBEDTLS_HAS_ALG_SHA_256_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_DIGEST_ALG_SHA256,    "id-sha256",    "SHA-256"),
        MBEDTLS_MD_SHA256,
    },
#endif
#if defined(MBEDTLS_HAS_ALG_SHA_384_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_DIGEST_ALG_SHA384,    "id-sha384",    "SHA-384"),
        MBEDTLS_MD_SHA384,
    },
#endif
#if defined(MBEDTLS_HAS_ALG_SHA_512_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_DIGEST_ALG_SHA512,    "id-sha512",    "SHA-512"),
        MBEDTLS_MD_SHA512,
    },
#endif
#if defined(MBEDTLS_HAS_ALG_RIPEMD160_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_DIGEST_ALG_RIPEMD160, "id-ripemd160", "RIPEMD-160"),
        MBEDTLS_MD_RIPEMD160,
    },
#endif
    {
        NULL_OID_DESCRIPTOR,
        MBEDTLS_MD_NONE,
    },
};

FN_OID_TYPED_FROM_ASN1(oid_md_alg_t, md_alg, oid_md_alg)
FN_OID_GET_ATTR1(mbedtls_oid_get_md_alg, oid_md_alg_t, md_alg, mbedtls_md_type_t, md_alg)
FN_OID_GET_OID_BY_ATTR1(mbedtls_oid_get_oid_by_md,
                        oid_md_alg_t,
                        oid_md_alg,
                        mbedtls_md_type_t,
                        md_alg)

/*
 * For HMAC digestAlgorithm
 */
typedef struct {
    mbedtls_oid_descriptor_t    descriptor;
    mbedtls_md_type_t           md_hmac;
} oid_md_hmac_t;

static const oid_md_hmac_t oid_md_hmac[] =
{
#if defined(MBEDTLS_HAS_ALG_SHA_1_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_HMAC_SHA1,      "hmacSHA1",      "HMAC-SHA-1"),
        MBEDTLS_MD_SHA1,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_1_VIA_LOWLEVEL_OR_PSA */
#if defined(MBEDTLS_HAS_ALG_SHA_224_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_HMAC_SHA224,    "hmacSHA224",    "HMAC-SHA-224"),
        MBEDTLS_MD_SHA224,
    },
#endif
#if defined(MBEDTLS_HAS_ALG_SHA_256_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_HMAC_SHA256,    "hmacSHA256",    "HMAC-SHA-256"),
        MBEDTLS_MD_SHA256,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_256_VIA_LOWLEVEL_OR_PSA */
#if defined(MBEDTLS_HAS_ALG_SHA_384_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_HMAC_SHA384,    "hmacSHA384",    "HMAC-SHA-384"),
        MBEDTLS_MD_SHA384,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_384_VIA_LOWLEVEL_OR_PSA */
#if defined(MBEDTLS_HAS_ALG_SHA_512_VIA_LOWLEVEL_OR_PSA)
    {
        OID_DESCRIPTOR(MBEDTLS_OID_HMAC_SHA512,    "hmacSHA512",    "HMAC-SHA-512"),
        MBEDTLS_MD_SHA512,
    },
#endif /* MBEDTLS_HAS_ALG_SHA_512_VIA_LOWLEVEL_OR_PSA */
    {
        NULL_OID_DESCRIPTOR,
        MBEDTLS_MD_NONE,
    },
};

FN_OID_TYPED_FROM_ASN1(oid_md_hmac_t, md_hmac, oid_md_hmac)
FN_OID_GET_ATTR1(mbedtls_oid_get_md_hmac, oid_md_hmac_t, md_hmac, mbedtls_md_type_t, md_hmac)

#if defined(MBEDTLS_PKCS12_C)
/*
 * For PKCS#12 PBEs
 */
typedef struct {
    mbedtls_oid_descriptor_t    descriptor;
    mbedtls_md_type_t           md_alg;
    mbedtls_cipher_type_t       cipher_alg;
} oid_pkcs12_pbe_alg_t;

static const oid_pkcs12_pbe_alg_t oid_pkcs12_pbe_alg[] =
{
    {
        OID_DESCRIPTOR(MBEDTLS_OID_PKCS12_PBE_SHA1_DES3_EDE_CBC,
                       "pbeWithSHAAnd3-KeyTripleDES-CBC",
                       "PBE with SHA1 and 3-Key 3DES"),
        MBEDTLS_MD_SHA1,      MBEDTLS_CIPHER_DES_EDE3_CBC,
    },
    {
        OID_DESCRIPTOR(MBEDTLS_OID_PKCS12_PBE_SHA1_DES2_EDE_CBC,
                       "pbeWithSHAAnd2-KeyTripleDES-CBC",
                       "PBE with SHA1 and 2-Key 3DES"),
        MBEDTLS_MD_SHA1,      MBEDTLS_CIPHER_DES_EDE_CBC,
    },
    {
        NULL_OID_DESCRIPTOR,
        MBEDTLS_MD_NONE, MBEDTLS_CIPHER_NONE,
    },
};

FN_OID_TYPED_FROM_ASN1(oid_pkcs12_pbe_alg_t, pkcs12_pbe_alg, oid_pkcs12_pbe_alg)
FN_OID_GET_ATTR2(mbedtls_oid_get_pkcs12_pbe_alg,
                 oid_pkcs12_pbe_alg_t,
                 pkcs12_pbe_alg,
                 mbedtls_md_type_t,
                 md_alg,
                 mbedtls_cipher_type_t,
                 cipher_alg)
#endif /* MBEDTLS_PKCS12_C */

#define OID_SAFE_SNPRINTF                               \
    do {                                                \
        if (ret < 0 || (size_t) ret >= n)              \
        return MBEDTLS_ERR_OID_BUF_TOO_SMALL;    \
                                                      \
        n -= (size_t) ret;                              \
        p += (size_t) ret;                              \
    } while (0)

/* Return the x.y.z.... style numeric string for the given OID */
int mbedtls_oid_get_numeric_string(char *buf, size_t size,
                                   const mbedtls_asn1_buf *oid)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    size_t i, n;
    unsigned int value;
    char *p;

    p = buf;
    n = size;

    /* First subidentifier contains first two OID components */
    i = 0;
    value = 0;
    if ((oid->p[0]) == 0x80) {
        /* Overlong encoding is not allowed */
        return MBEDTLS_ERR_ASN1_INVALID_DATA;
    }

    while (i < oid->len && ((oid->p[i] & 0x80) != 0)) {
        /* Prevent overflow in value. */
        if (value > (UINT_MAX >> 7)) {
            return MBEDTLS_ERR_ASN1_INVALID_DATA;
        }

        value |= oid->p[i] & 0x7F;
        value <<= 7;
        i++;
    }
    if (i >= oid->len) {
        return MBEDTLS_ERR_ASN1_OUT_OF_DATA;
    }
    /* Last byte of first subidentifier */
    value |= oid->p[i] & 0x7F;
    i++;

    unsigned int component1 = value / 40;
    if (component1 > 2) {
        /* The first component can only be 0, 1 or 2.
         * If oid->p[0] / 40 is greater than 2, the leftover belongs to
         * the second component. */
        component1 = 2;
    }
    unsigned int component2 = value - (40 * component1);
    ret = mbedtls_snprintf(p, n, "%u.%u", component1, component2);
    OID_SAFE_SNPRINTF;

    value = 0;
    for (; i < oid->len; i++) {
        /* Prevent overflow in value. */
        if (value > (UINT_MAX >> 7)) {
            return MBEDTLS_ERR_ASN1_INVALID_DATA;
        }
        if ((value == 0) && ((oid->p[i]) == 0x80)) {
            /* Overlong encoding is not allowed */
            return MBEDTLS_ERR_ASN1_INVALID_DATA;
        }

        value <<= 7;
        value |= oid->p[i] & 0x7F;

        if (!(oid->p[i] & 0x80)) {
            /* Last byte */
            ret = mbedtls_snprintf(p, n, ".%u", value);
            OID_SAFE_SNPRINTF;
            value = 0;
        }
    }

    return (int) (size - n);
}

static int oid_parse_number(unsigned int *num, const char **p, const char *bound)
{
    int ret = MBEDTLS_ERR_ASN1_INVALID_DATA;
    *num = 0;
    while (*p < bound && **p >= '0' && **p <= '9') {
        ret = 0;
        if (*num > (INT_MAX / 10)) {
            return MBEDTLS_ERR_ASN1_INVALID_DATA;
        }
        *num *= 10;
        *num += **p - '0';
        (*p)++;
    }
    return ret;
}

static size_t oid_subidentifier_num_bytes(unsigned int value)
{
    size_t num_bytes = 1;
    value >>= 7;
    while (value != 0) {
        num_bytes++;
        value >>= 7;
    }
    return num_bytes;
}

static int oid_subidentifier_encode_into(unsigned char **p,
                                         unsigned char *bound,
                                         unsigned int value)
{
    size_t num_bytes = oid_subidentifier_num_bytes(value);
    if ((size_t) (bound - *p) < num_bytes) {
        return MBEDTLS_ERR_OID_BUF_TOO_SMALL;
    }
    (*p)[num_bytes - 1] = (unsigned char) (value & 0x7f);
    value >>= 7;

    for (size_t i = 2; i <= num_bytes; i++) {
        (*p)[num_bytes - i] = 0x80 | (unsigned char) (value & 0x7f);
        value >>= 7;
    }
    *p += num_bytes;

    return 0;
}

/* Return the OID for the given x.y.z.... style numeric string  */
int mbedtls_oid_from_numeric_string(mbedtls_asn1_buf *oid,
                                    const char *oid_str, size_t size)
{
    int ret = MBEDTLS_ERR_ASN1_INVALID_DATA;

    const char *str_ptr = oid_str;
    const char *str_bound = oid_str + size;
    unsigned int val = 0;
    size_t encoded_len;
    unsigned int component1, component2;

    ret = oid_parse_number(&component1, &str_ptr, str_bound);
    if (ret != 0) {
        return ret;
    }
    if (component1 > 2) {
        /* First component can't be > 2 */
        return MBEDTLS_ERR_ASN1_INVALID_DATA;
    }
    if (str_ptr >= str_bound || *str_ptr != '.') {
        return MBEDTLS_ERR_ASN1_INVALID_DATA;
    }
    str_ptr++;

    ret = oid_parse_number(&component2, &str_ptr, str_bound);
    if (ret != 0) {
        return ret;
    }
    if ((component1 < 2) && (component2 > 39)) {
        /* Root nodes 0 and 1 may have up to 40 children, numbered 0-39 */
        return MBEDTLS_ERR_ASN1_INVALID_DATA;
    }
    if (str_ptr < str_bound && *str_ptr != '\0') {
        if (*str_ptr == '.') {
            str_ptr++;
        } else {
            return MBEDTLS_ERR_ASN1_INVALID_DATA;
        }
    }

    if ((UINT_MAX - component2) <= (component1 * 40)) {
        return MBEDTLS_ERR_ASN1_INVALID_DATA;
    }
    encoded_len = oid_subidentifier_num_bytes((component1 * 40) + component2);

    while (str_ptr < str_bound && *str_ptr != '\0') {
        oid_parse_number(&val, &str_ptr, str_bound);
        if (ret != 0) {
            return ret;
        }
        if (str_ptr < str_bound && *str_ptr != '\0') {
            if (*str_ptr == '.') {
                str_ptr++;
            } else {
                return MBEDTLS_ERR_ASN1_INVALID_DATA;
            }
        }

        size_t num_bytes = oid_subidentifier_num_bytes(val);
        if ((SIZE_MAX - encoded_len) <= num_bytes) {
            return MBEDTLS_ERR_ASN1_INVALID_DATA;
        }
        encoded_len += num_bytes;
    }

    oid->p = mbedtls_calloc(encoded_len, sizeof(unsigned char));
    if (oid->p == NULL) {
        return MBEDTLS_ERR_ASN1_ALLOC_FAILED;
    }
    oid->len = encoded_len;

    /* Now that we've allocated the buffer, go back to the start and encode */
    str_ptr = oid_str;
    unsigned char *out_ptr = oid->p;
    unsigned char *out_bound = oid->p + oid->len;

    /* No need to do validation this time, as we did it on the first pass */
    oid_parse_number(&component1, &str_ptr, str_bound);
    /* Skip past the '.' */
    str_ptr++;
    oid_parse_number(&component2, &str_ptr, str_bound);
    /* Skip past the '.' */
    str_ptr++;
    ret = oid_subidentifier_encode_into(&out_ptr, out_bound,
                                        (component1 * 40) + component2);
    if (ret != 0) {
        mbedtls_free(oid->p);
        oid->p = NULL;
        oid->len = 0;
        return ret;
    }
    while (str_ptr < str_bound && *str_ptr != '\0') {
        oid_parse_number(&val, &str_ptr, str_bound);
        if (str_ptr < str_bound && *str_ptr == '.') {
            /* Skip past the '.' */
            str_ptr++;
        }

        ret = oid_subidentifier_encode_into(&out_ptr, out_bound, val);
        if (ret != 0) {
            mbedtls_free(oid->p);
            oid->p = NULL;
            oid->len = 0;
            return ret;
        }
    }
    oid->tag = MBEDTLS_ASN1_OID;

    return 0;
}

#endif /* MBEDTLS_OID_C */
