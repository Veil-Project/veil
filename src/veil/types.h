#ifndef VEIL_TYPES_H
#define VEIL_TYPES_H


typedef std::vector<uint8_t> ec_point;

const size_t EC_SECRET_SIZE = 32;
const size_t EC_COMPRESSED_SIZE = 33;
const size_t EC_UNCOMPRESSED_SIZE = 65;

//typedef struct ec_secret { uint8_t e[EC_SECRET_SIZE]; } ec_secret;


#endif //VEIL_TYPES_H
