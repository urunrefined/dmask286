#include <algorithm>
#include <vector>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "File.h"

// RB,  - Register Byte
// EB   - Register Byte or Memory (Effective Byte Address)
// RW   - Register Word
// EW   - Register Word or Memory (Effective Word Address)

template <typename T1, size_t N> static size_t arraySize(T1 (&)[N]) {
    return N;
}
static const char *rb[] = {"AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"};
static const char *rw[] = {"AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"};
static const char *segments[] = {"ES", "CS", "SS", "DS"};

enum class Reg : uint8_t {
    AL = 0,
    AX = 0,
    CL = 1,
    CX = 1,
    DL = 2,
    DX = 2,
    BL = 3,
    BX = 3,
    AH = 4,
    SP = 4,
    CH = 5,
    BP = 5,
    DH = 6,
    SI = 6,
    BH = 7,
    DI = 7
};

enum class Width : uint8_t {
    NONE = 0,
    BYTE = 1,
    WORD = 2,
    DWORD = 4,
    QWORD = 8
};

enum class Segment : uint8_t {
    ES = 0b00,
    CS = 0b01,
    SS = 0b10,
    DS = 0b11,
    END = 0b100
};

enum class R_Type {
    NODISP = 0b00,
    DISP8 = 0b01,
    DISP16 = 0b10,
    REG = 0b11,
};

static const char *modNames[]{
    "BX + SI", "BX + DI", "BP + SI", "BP + DI", "SI", "DI", "BP", "BX",
};

static Width getDispMemWidth(uint8_t rm, R_Type disp) {
    if (disp == R_Type::NODISP) {
        if (rm == 0b110)
            return Width::WORD;
    } else if (disp == R_Type::DISP8) {
        return Width::BYTE;
    } else if (disp == R_Type::DISP16) {
        return Width::WORD;
    }

    return Width::NONE;
}

static const char *getWidthName(Width width) {
    switch (width) {
    case Width::BYTE:
        return "BYTE";
    case Width::WORD:
        return "WORD";
    case Width::DWORD:
        return "DWORD";
    case Width::QWORD:
        return "QWORD";
    default:
        return "MEM";
    }
}

enum class Type {
    NONE = 0,
    SEG,
    RB,
    RW,
    RMB,
    RMW,
    RMDW,
    RMQW,
    MEM,
    RNONE,
    DB,
    DW,
    DEREFBYTEATDW,
    DEREFWORDATDW,
    DDW,
    REGB,
    REGW,
    CSEG,
    AREGB,
    AREGW,
    CONSTBYTE,
    ST,
    STREG
};

struct D {
    Type type;
    uint32_t num;
};

struct Description {
    D d[3]{};
};

enum class OPExt { NONE, N, FPU_XY, FPU_11 };

struct Op {
    const char *name;
    const Description *description;
    OPExt opExt;

    uint8_t codeSz;
    uint8_t n;
    uint8_t code[2];

    Op(const uint8_t (&code2)[2], const char *name,
       const Description *description, OPExt opExt = OPExt::NONE, uint8_t n = 0)
        : name(name), description(description), opExt(opExt), codeSz(2), n(n) {
        memcpy(code, &code2, 2);
    }

    Op(const uint8_t code2, const char *name, const Description *description,
       OPExt opExt = OPExt::NONE, uint8_t n = 0)
        : name(name), description(description), opExt(opExt), codeSz(1), n(n) {
        code[0] = code2;
    }
};

// R
// 7    6 2   0 7        0 7         0 7       0 7        0
//  mod  r r/m | disp_low | disp_high | imm_low | imm_high

// clang-format off

static Description none;
static Description R_RMB_RB  =   { {{Type::RMB,   0},                {Type::RB, 0},  {} } };
static Description R_RMW_RW  =   { {{Type::RMW,   0},                {Type::RW, 0},  {} } };
static Description R_RB_RMB  =   { {{Type::RB,    0},                {Type::RMB, 0}, {} } };
static Description R_RW_RMW  =   { {{Type::RW,    0},                {Type::RMW, 0}, {} } };
static Description R_RW_RMDW =   { {{Type::RW,    0},                {Type::RMDW, 0}, {} } };
static Description R_RMB_DB  =   { {{Type::RMB,   0},                {Type::DB, 0},  {} } };
static Description R_RMW_DW  =   { {{Type::RMW,   0},                {Type::DW, 0},  {} } };
static Description R_RMW_DB  =   { {{Type::RMW,   0},                {Type::DB, 0},  {} } };
static Description R_RMW_SEG =   { {{Type::RMW,   0},                {Type::SEG, 0},  {} } };
static Description R_SEG_RMW =   { {{Type::SEG,   0},                {Type::RMW, 0},  {} } };

static Description R_RMB =      { {{Type::RMB,   0},                 {},             {} } };
static Description R_RMW =      { {{Type::RMW,   0},                 {},             {} } };
static Description R_RMDW =     { {{Type::RMDW,  0},                 {},             {} } };
static Description R_RMQW =     { {{Type::RMQW,  0},                 {},             {} } };

static Description R_RMB_C1     { {{Type::RMB,   0},                 {Type::CONSTBYTE, 1},             {} } };
static Description R_RMB_CL     { {{Type::RMB,   0},                 {Type::REGB,  (unsigned)Reg::CL}, {} } };

static Description R_RMW_C1     { {{Type::RMW,   0},                 {Type::CONSTBYTE, 1},             {} } };
static Description R_RMW_CL     { {{Type::RMW,   0},                 {Type::REGB,  (unsigned)Reg::CL}, {} } };

static Description R_MEM        { {{Type::MEM,   0},                 {},               {} } };
static Description R_RW_MEM     { {{Type::RW,    0},                 {Type::MEM,  0},  {} } };

static Description R_RW_RMW_DB = { {{Type::RW,   0},                 {Type::RMW, 0}, {Type::DB, 0} } };
static Description R_RW_RMW_DW = { {{Type::RW,   0},                 {Type::RMW, 0}, {Type::DW, 0} } };


static Description I_DB =       { {{Type::DB,    0},                 {},               {} } };
static Description I_DW =       { {{Type::DW,    0},                 {},               {} } };
static Description I_DDW =      { {{Type::DDW,   0},                 {},               {} } };
static Description I_DW_DB =    { {{Type::DW,    0},                 {Type::DB,    0}, {} } };

static Description I_AL_DEREFBYTEATDW = { {{Type::REGB,  (unsigned)Reg::AL},  {Type::DEREFBYTEATDW,    0}, {} } };
static Description I_AX_DEREFWORDATDW = { {{Type::REGW,  (unsigned)Reg::AX},  {Type::DEREFWORDATDW,    0}, {} } };

static Description I_DEREFBYTEATDW_AL = { {{Type::DEREFBYTEATDW,    0}, {Type::REGB,  (unsigned)Reg::AL},  {} } };
static Description I_DEREFBYTEATDW_AX = { {{Type::DEREFWORDATDW,    0}, {Type::REGW,  (unsigned)Reg::AX},  {} } };


static Description REG_AX_DB =  { {{Type::REGW,  (unsigned)Reg::AX}, {Type::DB, 0},  {} } };

static Description REG_DB_AL =  { {{Type::DB, 0}, {Type::REGW,  (unsigned)Reg::AX},   {} } };
static Description REG_DB_AX =  { {{Type::DB, 0}, {Type::REGW,  (unsigned)Reg::AX},   {} } };

static Description REG_DX_AL =  { {{Type::REGW,  (unsigned)Reg::DX}, {Type::REGB,  (unsigned)Reg::AL},   {} } };
static Description REG_DX_AX =  { {{Type::REGW,  (unsigned)Reg::DX}, {Type::REGW,  (unsigned)Reg::AX},   {} } };

static Description REG_AL_DB =  { {{Type::REGB,  (unsigned)Reg::AL}, {Type::DB, 0},  {} } };
static Description REG_CL_DB =  { {{Type::REGB,  (unsigned)Reg::CL}, {Type::DB, 0},  {} } };
static Description REG_DL_DB =  { {{Type::REGB,  (unsigned)Reg::DL}, {Type::DB, 0},  {} } };
static Description REG_BL_DB =  { {{Type::REGB,  (unsigned)Reg::BL}, {Type::DB, 0},  {} } };
static Description REG_AH_DB =  { {{Type::REGB,  (unsigned)Reg::AH}, {Type::DB, 0},  {} } };
static Description REG_CH_DB =  { {{Type::REGB,  (unsigned)Reg::CH}, {Type::DB, 0},  {} } };
static Description REG_DH_DB =  { {{Type::REGB,  (unsigned)Reg::DH}, {Type::DB, 0},  {} } };
static Description REG_BH_DB =  { {{Type::REGB,  (unsigned)Reg::BH}, {Type::DB, 0},  {} } };

static Description REG_AX_DW =  { {{Type::REGW,  (unsigned)Reg::AX}, {Type::DW, 0},  {} } };
static Description REG_CX_DW =  { {{Type::REGW,  (unsigned)Reg::CX}, {Type::DW, 0},  {} } };
static Description REG_DX_DW =  { {{Type::REGW,  (unsigned)Reg::DX}, {Type::DW, 0},  {} } };
static Description REG_BX_DW =  { {{Type::REGW,  (unsigned)Reg::BX}, {Type::DW, 0},  {} } };
static Description REG_SP_DW =  { {{Type::REGW,  (unsigned)Reg::SP}, {Type::DW, 0},  {} } };
static Description REG_BP_DW =  { {{Type::REGW,  (unsigned)Reg::BP}, {Type::DW, 0},  {} } };
static Description REG_SI_DW =  { {{Type::REGW,  (unsigned)Reg::SI}, {Type::DW, 0},  {} } };
static Description REG_DI_DW =  { {{Type::REGW,  (unsigned)Reg::DI}, {Type::DW, 0},  {} } };

static Description REG_AX    =  { {{Type::REGW,  (unsigned)Reg::AX}, {},             {} } };
static Description REG_CX    =  { {{Type::REGW,  (unsigned)Reg::CX}, {},             {} } };
static Description REG_DX    =  { {{Type::REGW,  (unsigned)Reg::DX}, {},             {} } };
static Description REG_BX    =  { {{Type::REGW,  (unsigned)Reg::BX}, {},             {} } };
static Description REG_SP    =  { {{Type::REGW,  (unsigned)Reg::SP}, {},             {} } };
static Description REG_BP    =  { {{Type::REGW,  (unsigned)Reg::BP}, {},             {} } };
static Description REG_SI    =  { {{Type::REGW,  (unsigned)Reg::SI}, {},             {} } };
static Description REG_DI    =  { {{Type::REGW,  (unsigned)Reg::DI}, {},             {} } };

static Description REG_AX_AX    =  { {{Type::REGW,  (unsigned)Reg::AX},    {Type::REGW,  (unsigned)Reg::AX},              {} } };
static Description REG_AX_CX    =  { {{Type::REGW,  (unsigned)Reg::AX},    {Type::REGW,  (unsigned)Reg::CX},              {} } };
static Description REG_AX_DX    =  { {{Type::REGW,  (unsigned)Reg::AX},    {Type::REGW,  (unsigned)Reg::DX},              {} } };
static Description REG_AX_BX    =  { {{Type::REGW,  (unsigned)Reg::AX},    {Type::REGW,  (unsigned)Reg::BX},              {} } };
static Description REG_AX_SP    =  { {{Type::REGW,  (unsigned)Reg::AX},    {Type::REGW,  (unsigned)Reg::SP},              {} } };
static Description REG_AX_BP    =  { {{Type::REGW,  (unsigned)Reg::AX},    {Type::REGW,  (unsigned)Reg::BP},              {} } };
static Description REG_AX_SI    =  { {{Type::REGW,  (unsigned)Reg::AX},    {Type::REGW,  (unsigned)Reg::SI},              {} } };
static Description REG_AX_DI    =  { {{Type::REGW,  (unsigned)Reg::AX},    {Type::REGW,  (unsigned)Reg::DI},              {} } };

static Description REG_DS    =  { {{Type::CSEG,  (unsigned)Segment::DS}, {},             {} } };
static Description REG_CS    =  { {{Type::CSEG,  (unsigned)Segment::CS}, {},             {} } };
static Description REG_ES    =  { {{Type::CSEG,  (unsigned)Segment::ES}, {},             {} } };
static Description REG_SS    =  { {{Type::CSEG,  (unsigned)Segment::SS}, {},             {} } };

static Description REG_AL_DX =  { {{Type::REGB,  (unsigned)Reg::AL}, {Type::REGW,  (unsigned)Reg::DX},  {} } };

static Description F_ST_STREG = { {{Type::ST, 0},    {Type::STREG, 0}, {} } };
static Description F_STREG_ST = { {{Type::STREG, 0}, {Type::ST, 0},    {} } };
static Description F_STREG    = { {{Type::STREG, 0}, {},               {} } };

static Op ops [] = {
        {0x37,             "AAA",   &none               },
        {{0xD5, 0x0A},     "AAD",   &none               },
        {{0xD4, 0x0A},     "AAM",   &none               },
        {0x3F,             "AAS",   &none               },
        
        {0x10,             "ADC",   &R_RMB_RB           },
        {0x11,             "ADC",   &R_RMW_RW           },
        {0x12,             "ADC",   &R_RB_RMB           },
        {0x13,             "ADC",   &R_RW_RMW           },
        {0x14,             "ADC",   &REG_AL_DB          },
        {0x15,             "ADC",   &REG_AX_DW          },
        {0x80,             "ADC",   &R_RMB_DB,  OPExt::N, 2 },
        {0x81,             "ADC",   &R_RMW_DW,  OPExt::N, 2 },
        {0x83,             "ADC",   &R_RMW_DB,  OPExt::N, 2 },
        
        {0x00,             "ADD",   &R_RMB_RB           },
        {0x01,             "ADD",   &R_RMW_RW           },
        {0x02,             "ADD",   &R_RB_RMB           },
        {0x03,             "ADD",   &R_RW_RMW           },
        {0x04,             "ADD",   &REG_AL_DB          },
        {0x05,             "ADD",   &REG_AX_DW          },
        {0x80,             "ADD",   &R_RMB_DB,  OPExt::N, 0 },
        {0x81,             "ADD",   &R_RMW_DW,  OPExt::N, 0 },
        {0x83,             "ADD",   &R_RMW_DB,  OPExt::N, 0 },
        {0x20,             "AND",   &R_RMB_RB           },
        {0x21,             "AND",   &R_RMW_RW           },
        {0x22,             "AND",   &R_RB_RMB           },
        {0x23,             "AND",   &R_RW_RMW           },
        {0x24,             "AND",   &REG_AL_DB         },
        {0x25,             "AND",   &REG_AX_DW         },
        {0x80,             "AND",   &R_RMB_DB,  OPExt::N, 4 },
        {0x81,             "AND",   &R_RMW_DW,  OPExt::N, 4 },
        {0x63,             "ARPL",  &R_RMW_RW           },
        
        {0x62,             "BOUND", &R_RW_RMW           },
        
        {0xE8,             "CALL",  &I_DW               },
        {0xFF,             "CALL",  &R_RMW,     OPExt::N, 2 },
        {0x9A,             "CALL",  &I_DDW              },
        {0xFF,             "CALL",  &R_RMDW,    OPExt::N, 3 },
        
        {0x98,             "CBW",   &none               },        
        {0xF8,             "CLC",   &none               },
        {0xFC,             "CLD",   &none               },
        {0xFA,             "CLI",   &none               },
        {{0x0F,0x06},      "CLTS",  &none               },
        {0xF5,             "CMC",   &none               },
        
        {0x3C,             "CMP",   &REG_AL_DB          },
        {0x3D,             "CMP",   &REG_AX_DW          },
        {0x80,             "CMP",   &R_RMB_DB,  OPExt::N, 7 },
        {0x38,             "CMP",   &R_RMW_RW           },
        {0x83,             "CMP",   &R_RMW_DB,  OPExt::N, 7 },
        {0x81,             "CMP",   &R_RMW_DW,  OPExt::N, 7 },
        {0x39,             "CMP",   &R_RMW_RW           },
        {0x3A,             "CMP",   &R_RB_RMB           },
        {0x3B,             "CMP",   &R_RW_RMW           },
        
        {0xA6,             "CMPSB", &none               },
        {0xA7,             "CMPSW", &none               },
        
        {0x99,             "CWD",   &none               },
        
        {0x27,             "DAA",   &none               },
        {0x2F,             "DAS",   &none               },
        
        {0xFE,             "DEC",   &R_RMB,     OPExt::N, 1 },
        {0xFF,             "DEC",   &R_RMW,     OPExt::N, 1 },
        {0x48,             "DEC",   &REG_AX,            },
        {0x49,             "DEC",   &REG_CX,            },
        {0x4A,             "DEC",   &REG_DX,            },
         {0x4B,             "DEC",   &REG_BX,            },
        {0x4C,             "DEC",   &REG_SP,            },
        {0x4D,             "DEC",   &REG_BP,            },
        {0x4E,             "DEC",   &REG_SI,            },
        {0x4F,             "DEC",   &REG_DI,            },
        
        {0xF6,             "DIV",   &R_RMB,     OPExt::N, 6 },
        {0xF7,             "DIV",   &R_RMW,     OPExt::N, 6 },
        
        {0xC8,             "ENTER", &I_DW_DB,           },
        
        {0xF4,             "HLT",   &none               },
        
        {0xF6,             "IDIV",  &R_RMB,     OPExt::N, 7 },
        {0xF7,             "IDIV",  &R_RMW,     OPExt::N, 7 },
        
        {0xF6,             "IMUL",  &R_RMB,     OPExt::N, 5 },
        {0xF7,             "IMUL",  &R_RMW,     OPExt::N, 5 },
        {0x6B,             "IMUL",  &R_RW_RMW_DB,       },
        {0x69,             "IMUL",  &R_RW_RMW_DW,       },
        
        {0xE4,             "IN",    &REG_AL_DB          },
        {0xEC,             "IN",    &REG_AL_DX          },
        {0xE5,             "IN",    &REG_AX_DB          },
        {0xED,             "IN",    &REG_AX_DX          },
        
        {0xFE,             "INC",   &R_RMB,     OPExt::N, 0 },
        {0xFF,             "INC",   &R_RMW,     OPExt::N, 0 },
        {0x40,             "INC",   &REG_AX,            },
        {0x41,             "INC",   &REG_CX,            },
        {0x42,             "INC",   &REG_DX,            },
        {0x43,             "INC",   &REG_BX,            },
        {0x44,             "INC",   &REG_SP,            },
        {0x45,             "INC",   &REG_BP,            },
        {0x46,             "INC",   &REG_SI,            },
        {0x47,             "INC",   &REG_DI,            },
        
        {0x6C,             "INSB",  &none               },
        {0x6D,             "INSW",  &none               },
        
        {0xCC,             "INT3",  &none               },
        {0xCD,             "INT",   &I_DB               },
        {0xCE,             "INTO",  &none               },
        {0xCF,             "IRET",  &none               },
        
        {0x77,             "JA",    &I_DB               },
        {0x73,             "JAE",   &I_DB               },
        {0x72,             "JB",    &I_DB               },
        {0x76,             "JBE",   &I_DB               },
        {0x72,             "JC",    &I_DB               },
        {0xE3,             "JCXZ",  &I_DB               },
        {0x74,             "JE",    &I_DB               },
        {0x7F,             "JG",    &I_DB               },
        {0x7D,             "JGE",   &I_DB               },
        {0x7C,             "JL",    &I_DB               },
        {0x7E,             "JLE",   &I_DB               },
        {0x76,             "JNA",   &I_DB               },
        {0x72,             "JNAE",  &I_DB               },
        {0x73,             "JNB",   &I_DB               },
        {0x77,             "JNBE",  &I_DB               },
        {0x73,             "JNC",   &I_DB               },
        {0x75,             "JNE",   &I_DB               },
        {0x7E,             "JNG",   &I_DB               },
        {0x7C,             "JNGE",  &I_DB               },
        {0x7D,             "JNL",   &I_DB               },
        {0x7F,             "JNLE",  &I_DB               },
        {0x71,             "JNO",   &I_DB               },
        {0x7B,             "JNP",   &I_DB               },
        {0x79,             "JNS",   &I_DB               },
        {0x75,             "JNZ",   &I_DB               },
        {0x70,             "JO",    &I_DB               },
        {0x7A,             "JP",    &I_DB               },
        {0x7A,             "JPE",   &I_DB               },
        {0x7B,             "JPO",   &I_DB               },
        {0x78,             "JS",    &I_DB               },
        {0x74,             "JZ",    &I_DB               },
        {0xEB,             "JMP",   &I_DB               },
        {0xEA,             "JMP",   &I_DDW              },
        {0xE9,             "JMP",   &I_DW               },
        {0xFF,             "JMP",   &R_RMW,     OPExt::N, 4 },
        {0xFF,             "JMP",   &R_RMDW,    OPExt::N, 5 },
        
        {0x9F,             "LAHF",  &none,              },
        {{0x0F, 0x02},     "LAR",   &R_RW_RMW,          },
        {0xC5,             "LDS",   &R_RW_RMDW,         },
        {0xC4,             "LES",   &R_RW_RMDW,         },
        
        {0x8D,             "LEA",   &R_RW_MEM,          },
        
        {0xC9,             "LEAVE", &none,              },
        
        {{0x0F, 0x01 },    "LGDT",  &R_MEM,     OPExt::N, 2 },
        {{0x0F, 0x01 },    "LIDT",  &R_MEM,     OPExt::N, 3 },
        {{0x0F, 0x00 },    "LLDT",  &R_RMW,     OPExt::N, 2 },
        {{0x0F, 0x01 },    "LMSW",  &R_RMW,     OPExt::N, 6 },
        
        {{0x0F, 0x05},     "LOADALL286", &none             },

        {0xF0,             "LOCK",  &none,              },
        
        {0xAC,             "LODSB", &none,              },
        {0xAD,             "LODSW", &none,              },
        
        {0xE2,             "LOOP",  &I_DB,              },
        {0xE1,             "LOOPE", &I_DB,              },
        {0xE0,             "LOOPNE",&I_DB,              },
        
        {{0x0F, 0x03},     "LSL",   &R_RW_RMW,          },
        
        {{0x0F, 0x00},     "LTR",   &R_RMW,    OPExt::N, 3  },
        
        {0x88,             "MOV",   &R_RMB_RB,          },
        {0x89,             "MOV",   &R_RMW_RW,          },
        {0x8A,             "MOV",   &R_RB_RMB,          },
        {0x8B,             "MOV",   &R_RW_RMW,          },
        {0x8C,             "MOV",   &R_RMW_SEG, OPExt::N, 0  },
        {0x8C,             "MOV",   &R_RMW_SEG, OPExt::N, 1  },
        {0x8C,             "MOV",   &R_RMW_SEG, OPExt::N, 2  },
        {0x8C,             "MOV",   &R_RMW_SEG, OPExt::N, 3  },
        {0x8E,             "MOV",   &R_SEG_RMW, OPExt::N, 0  },
        //Nothin in the spec
        //{0x8C,             "MOV",   &R_SEG_RMW, true, 1  },
        {0x8E,             "MOV",   &R_SEG_RMW, OPExt::N, 2  },
        {0x8E,             "MOV",   &R_SEG_RMW, OPExt::N, 3  },
        {0xA0,             "MOV",   &I_AL_DEREFBYTEATDW  },
        {0xA1,             "MOV",   &I_AX_DEREFWORDATDW  },
        {0xA2,             "MOV",   &I_DEREFBYTEATDW_AL  },
        {0xA3,             "MOV",   &I_DEREFBYTEATDW_AX  },
        {0xB0,             "MOV",   &REG_AL_DB},
        {0xB1,             "MOV",   &REG_CL_DB},
        {0xB2,             "MOV",   &REG_DL_DB},
        {0xB3,             "MOV",   &REG_BL_DB},
        {0xB4,             "MOV",   &REG_AH_DB},
        {0xB5,             "MOV",   &REG_CH_DB},
        {0xB6,             "MOV",   &REG_DH_DB},
        {0xB7,             "MOV",   &REG_BH_DB},
        {0xB8,             "MOV",   &REG_AX_DW},
        {0xB9,             "MOV",   &REG_CX_DW},
        {0xBA,             "MOV",   &REG_DX_DW},
        {0xBB,             "MOV",   &REG_BX_DW},
        {0xBC,             "MOV",   &REG_SP_DW},
        {0xBD,             "MOV",   &REG_BP_DW},
        {0xBE,             "MOV",   &REG_SI_DW},
        {0xBF,             "MOV",   &REG_DI_DW},
        {0xC6,             "MOV",   &R_RMB_DB},
        {0xC7,             "MOV",   &R_RMW_DW},
        {0xA4,             "MOVSB", &none },
        {0xA5,             "MOVSW", &none },
        
        {0xF6,             "MUL",   &R_RMB,   OPExt::N, 4 },
        {0xF7,             "MUL",   &R_RMW,   OPExt::N, 4 },
        
        {0xF6,             "NEG",   &R_RMB,   OPExt::N, 3 },
        {0xF7,             "NEG",   &R_RMW,   OPExt::N, 3 },
        
        {0x90,             "NOP",   &none },
        
        {0xF6,             "NOT",   &R_RMB,   OPExt::N, 2 },
        {0xF7,             "NOT",   &R_RMW,   OPExt::N, 2 },        
        
        {0x08,             "OR",    &R_RMB_RB          },
        {0x09,             "OR",    &R_RMW_RW          },
        {0x0A,             "OR",    &R_RB_RMB          },
        {0x0B,             "OR",    &R_RW_RMW          },
        {0x0C,             "OR",    &REG_AL_DB         },
        {0x0D,             "OR",    &REG_AX_DW         },
        {0x80,             "OR",    &R_RMB_DB, OPExt::N, 1 },
        {0x81,             "OR",    &R_RMW_DW, OPExt::N, 1 },
        
        {0xE6,             "OUT",   &REG_DB_AL,        },
        {0xE7,             "OUT",   &REG_DB_AX,        },
        {0xEE,             "OUT",   &REG_DX_AL,        },
        {0xEF,             "OUT",   &REG_DX_AX,        },
        {0x6E,             "OUTSB", &none,             },
        {0x6F,             "OUTSW", &none,             },
        
        {0x1F,             "POP",   &REG_DS,           },
        //Nothing in the spec
        {0x07,             "POP",   &REG_ES,           },
        {0x17,             "POP",   &REG_SS,           },
        {0x8F,             "POP",   &R_RMW,   OPExt::N, 0  },
        {0x58,             "POP",   &REG_AX,           },
        {0x59,             "POP",   &REG_CX,           },
        {0x5A,             "POP",   &REG_DX,           },
        {0x5B,             "POP",   &REG_BX,           },
        {0x5C,             "POP",   &REG_SP,           },
        {0x5D,             "POP",   &REG_BP,           },
        {0x5E,             "POP",   &REG_SI,           },
        {0x5F,             "POP",   &REG_DI,           },
        {0x61,             "POPA",  &none,             },
        {0x9D,             "POPF",  &none,             },
        {0x06,             "PUSH",  &REG_ES,           },
        {0x0E,             "PUSH",  &REG_CS,           },
        {0x16,             "PUSH",  &REG_SS,           },
        {0x1E,             "PUSH",  &REG_DS,           },
        {0x50,             "PUSH",  &REG_AX,           },
        {0x51,             "PUSH",  &REG_CX,           },
        {0x52,             "PUSH",  &REG_DX,           },
        {0x53,             "PUSH",  &REG_BX,           },
        {0x54,             "PUSH",  &REG_SP,           },
        {0x55,             "PUSH",  &REG_BP,           },
        {0x56,             "PUSH",  &REG_SI,           },
        {0x57,             "PUSH",  &REG_DI,           },
        {0xFF,             "PUSH",  &R_RMW,   OPExt::N, 6  },
        {0x68,             "PUSH",  &I_DW,             },
        {0x6A,             "PUSH",  &I_DB,             },
        {0x60,             "PUSHA", &none,             },
        {0x9C,             "PUSHF", &none,             },
        
        {0xD0,             "RCL",   &R_RMB_C1, OPExt::N, 2 },
        {0xD2,             "RCL",   &R_RMB_CL, OPExt::N, 2 },
        {0xC0,             "RCL",   &R_RMB_DB, OPExt::N, 2 },
        {0xD1,             "RCL",   &R_RMW_C1, OPExt::N, 2 },
        {0xD3,             "RCL",   &R_RMW_CL, OPExt::N, 2 },
        {0xC1,             "RCL",   &R_RMW_DB, OPExt::N, 2 },
        {0xD0,             "RCR",   &R_RMB_C1, OPExt::N, 3 },
        {0xD2,             "RCR",   &R_RMB_CL, OPExt::N, 3 },
        {0xC0,             "RCR",   &R_RMB_DB, OPExt::N, 3 },
        {0xD1,             "RCR",   &R_RMW_C1, OPExt::N, 3 },
        {0xD3,             "RCR",   &R_RMW_CL, OPExt::N, 3 },
        {0xC1,             "RCR",   &R_RMW_DB, OPExt::N, 3 },
        {0xD0,             "ROL",   &R_RMB_C1, OPExt::N, 0 },
        {0xD2,             "ROL",   &R_RMB_CL, OPExt::N, 0 },
        {0xC0,             "ROL",   &R_RMB_DB, OPExt::N, 0 },
        {0xD1,             "ROL",   &R_RMW_C1, OPExt::N, 0 },
        {0xD3,             "ROL",   &R_RMW_CL, OPExt::N, 0 },
        {0xC1,             "ROL",   &R_RMW_DB, OPExt::N, 0 },
        {0xD0,             "ROR",   &R_RMB_C1, OPExt::N, 1 },
        {0xD2,             "ROR",   &R_RMB_CL, OPExt::N, 1 },
        {0xC0,             "ROR",   &R_RMB_DB, OPExt::N, 1 },
        {0xD1,             "ROR",   &R_RMW_C1, OPExt::N, 1 },
        {0xD3,             "ROR",   &R_RMW_CL, OPExt::N, 1 },
        {0xC1,             "ROR",   &R_RMW_DB, OPExt::N, 1 },        
        
        {{0xF3, 0x6C},     "REP INSB", &none           },
        {{0xF3, 0x6D},     "REP INSW", &none           },
        {{0xF3, 0xA4},     "REP MOVSB", &none          },
        {{0xF3, 0xA5},     "REP MOVSW", &none          },
        {{0xF3, 0x6E},     "REP OUTSB", &none          },
        {{0xF3, 0x6F},     "REP OUTSW", &none          },
        {{0xF3, 0xAA},     "REP STOSB", &none          },
        {{0xF3, 0xAB},     "REP STOSW", &none          },
        {{0xF3, 0xA6},     "REPE CMPSB", &none         },
        {{0xF3, 0xA7},     "REPE CMPSW", &none         },
        {{0xF3, 0xAE},     "REPE SCASB", &none         },
        {{0xF3, 0xAF},     "REPE SCASW", &none         },
        {{0xF2, 0xA6},     "REPNE CMPSB", &none         },
        {{0xF2, 0xA7},     "REPNE CMPSW", &none         },
        {{0xF2, 0xAE},     "REPNE SCASB", &none         },
        {{0xF2, 0xAF},     "REPNE SCASW", &none         },
        
        {0xCB,             "RET",         &none         },
        {0xC3,             "RET",         &none         },
        {0xCA,             "RETF",        &I_DW         },
        {0xC2,             "RET",         &I_DW         },
        
        {0x9E,             "SAHF",        &none         },
        
        {0xD0,             "SAL",   &R_RMB_C1, OPExt::N, 4 },
        {0xD2,             "SAL",   &R_RMB_CL, OPExt::N, 4 },
        {0xC0,             "SAL",   &R_RMB_DB, OPExt::N, 4 },
        {0xD1,             "SAL",   &R_RMW_C1, OPExt::N, 4 },
        {0xD3,             "SAL",   &R_RMW_CL, OPExt::N, 4 },
        {0xC1,             "SAL",   &R_RMW_DB, OPExt::N, 4 },
        {0xD0,             "SAR",   &R_RMB_C1, OPExt::N, 7 },
        {0xD2,             "SAR",   &R_RMB_CL, OPExt::N, 7 },
        {0xC0,             "SAR",   &R_RMB_DB, OPExt::N, 7 },
        {0xD1,             "SAR",   &R_RMW_C1, OPExt::N, 7 },
        {0xD3,             "SAR",   &R_RMW_CL, OPExt::N, 7 },
        {0xC1,             "SAR",   &R_RMW_DB, OPExt::N, 7 },
        {0xD0,             "SHR",   &R_RMB_C1, OPExt::N, 5 },
        {0xD2,             "SHR",   &R_RMB_CL, OPExt::N, 5 },
        {0xC0,             "SHR",   &R_RMB_DB, OPExt::N, 5 },
        {0xD1,             "SHR",   &R_RMW_C1, OPExt::N, 5 },
        {0xD3,             "SHR",   &R_RMW_CL, OPExt::N, 5 },
        {0xC1,             "SHR",   &R_RMW_DB, OPExt::N, 5 },
        
        {0x18,             "SBB",   &R_RMB_RB          },
        {0x19,             "SBB",   &R_RMW_RW          },
        {0x1A,             "SBB",   &R_RB_RMB          },
        {0x1B,             "SBB",   &R_RW_RMW          },
        {0x1C,             "SBB",   &REG_AL_DB         },
        {0x1D,             "SBB",   &REG_AX_DW         },
        {0x80,             "SBB",   &R_RMB_DB,  OPExt::N, 3 },
        {0x81,             "SBB",   &R_RMW_DW,  OPExt::N, 3 },
        {0x83,             "SBB",   &R_RMW_DB,  OPExt::N, 3 },
        
        {0xAE,             "SCASB", &none               },
        {0xAF,             "SCASW", &none               },        
        {{0x0F, 0x01 },    "SGDT",  &R_MEM,     OPExt::N, 0 },
        {{0x0F, 0x01 },    "SIDT",  &R_MEM,     OPExt::N, 1 },
        {{0x0F, 0x00 },    "SLDT",  &R_RMW,     OPExt::N, 0 },
        {{0x0F, 0x01 },    "SMSW",  &R_RMW,     OPExt::N, 4 },
        {0xF9,             "STC",   &none               },
        {0xFD,             "STD",   &none               },
        {0xFB,             "STI",   &none               },
        {0xAA,             "STOSB", &none               },
        {0xAB,             "STOSW", &none               },
        {{0x0F, 0x00 },    "STR",   &R_RMW,      OPExt::N, 1},
        
        {0x28,             "SUB",   &R_RMB_RB           },
        {0x29,             "SUB",   &R_RMW_RW           },
        {0x2A,             "SUB",   &R_RB_RMB           },
        {0x2B,             "SUB",   &R_RW_RMW           },
        {0x2C,             "SUB",   &REG_AL_DB          },
        {0x2D,             "SUB",   &REG_AX_DW          },
        {0x80,             "SUB",   &R_RMB_DB,  OPExt::N, 5 },
        {0x81,             "SUB",   &R_RMW_DW,  OPExt::N, 5 },
        {0x83,             "SUB",   &R_RMW_DB,  OPExt::N, 5 },
        
        {0x84,             "TEST",  &R_RMB_RB           },
        {0x85,             "TEST",  &R_RMW_RW           },
        {0xA8,             "TEST",  &REG_AL_DB          },
        {0xA9,             "TEST",  &REG_AX_DW          },
        {0xF6,             "TEST",  &R_RMB_DB, OPExt::N, 0  },
        {0xF7,             "TEST",  &R_RMW_DW, OPExt::N, 0  },

        {{0x0F, 0x00 },    "VERR",  &R_RMW,    OPExt::N, 4  },
        {{0x0F, 0x00 },    "VERW",  &R_RMW,    OPExt::N, 5  },
        {0x9B,             "WAIT",  &none               },
        {0x86,             "XCHG",  &R_RMB_RB           },
        {0x87,             "XCHG",  &R_RMW_RW           },

        {0x90,             "XCHG",   &REG_AX_AX,        },
        {0x91,             "XCHG",   &REG_AX_CX,        },
        {0x92,             "XCHG",   &REG_AX_DX,        },
        {0x93,             "XCHG",   &REG_AX_BX,        },
        {0x94,             "XCHG",   &REG_AX_SP,        },
        {0x95,             "XCHG",   &REG_AX_BP,        },
        {0x96,             "XCHG",   &REG_AX_SI,        },
        {0x97,             "XCHG",   &REG_AX_DI,        },

        {0xD7,             "XLATB",  &none,             },

        {0x30,             "XOR",   &R_RMB_RB           },
        {0x31,             "XOR",   &R_RMW_RW           },
        {0x32,             "XOR",   &R_RB_RMB           },
        {0x33,             "XOR",   &R_RW_RMW           },
        {0x34,             "XOR",   &REG_AL_DB          },
        {0x35,             "XOR",   &REG_AX_DW          },
        {0x80,             "XOR",   &R_RMB_DB,  OPExt::N, 6 },
        {0x81,             "XOR",   &R_RMW_DW,  OPExt::N, 6 },



        {0xD8,             "FADD",   &R_RMDW,        OPExt::FPU_XY,  0},
        {0xD8,             "FADD",   &F_ST_STREG,    OPExt::FPU_11,  0},
        {0xD8,             "FMUL",   &R_RMDW,        OPExt::FPU_XY,  1},
        {0xD8,             "FMUL",   &F_ST_STREG,    OPExt::FPU_11,  1},
        {0xD8,             "FCOM",   &R_RMDW,        OPExt::FPU_XY,  2},
        {0xD8,             "FCOM",   &F_STREG,       OPExt::FPU_11,  2},
        {0xD8,             "FCOMP",  &R_RMDW,        OPExt::FPU_XY,  3},
        {0xD8,             "FCOMP",  &F_STREG,       OPExt::FPU_11,  3},
        {0xD8,             "FSUB",   &R_RMDW,        OPExt::FPU_XY,  4},
        {0xD8,             "FSUB",   &F_ST_STREG,    OPExt::FPU_11,  4},
        {0xD8,             "FSUBR",  &R_RMDW,        OPExt::FPU_XY,  5},
        {0xD8,             "FSUBR",  &F_ST_STREG,    OPExt::FPU_11,  5},
        {0xD8,             "FDIV",   &R_RMDW,        OPExt::FPU_XY,  6},
        {0xD8,             "FDIV",   &F_ST_STREG,    OPExt::FPU_11,  6},
        {0xD8,             "FDIVR",  &R_RMDW,        OPExt::FPU_XY,  7},
        {0xD8,             "FDIVR",  &F_ST_STREG,    OPExt::FPU_11,  7},


        {0xD9,             "FLD",          &R_RMDW,      OPExt::FPU_XY,  0},
        {0xD9,             "FLD",          &F_STREG,     OPExt::FPU_11,  0},
//      {0xD9,             "FPU Reserved", &I_DB,        OPExt::FPU_XY,  1},
        {0xD9,             "FXCH",         &F_STREG,     OPExt::FPU_11,  1},
        {0xD9,             "FST",          &R_RMDW,      OPExt::FPU_XY,  2},
        {0xD9,             "FSTP",         &R_RMDW,      OPExt::FPU_XY,  3},
        {0xD9,             "FSTP",         &F_STREG,     OPExt::FPU_11,  3},
        {0xD9,             "FLDENV",       &R_MEM,       OPExt::FPU_XY,  4},
        {0xD9,             "FLDCW",        &R_RMDW,      OPExt::FPU_XY,  5},
        {0xD9,             "FSTENV",       &R_MEM,       OPExt::FPU_XY,  6},
        {0xD9,             "FSTCW",        &R_RMDW,      OPExt::FPU_XY,  7},

        {{0xD9, 0xD0},     "FNOP",         &none                      },
        {{0xD9, 0xE0},     "FCHS",         &none                      },
        {{0xD9, 0xE1},     "FABS",         &none                      },
        {{0xD9, 0xE4},     "FTST",         &none                      },
        {{0xD9, 0xE5},     "FXAM",         &none                      },
        {{0xD9, 0xE8},     "FLD1",         &none                      },

        {{0xD9, 0xE9},     "FLDL2T",       &none                      },
        {{0xD9, 0xEA},     "FLDL2E",       &none                      },
        {{0xD9, 0xEB},     "FLDPI",        &none                      },
        {{0xD9, 0xEC},     "FLDLG2",       &none                      },
        {{0xD9, 0xED},     "FLDLN2",       &none                      },
        {{0xD9, 0xEE},     "FLDZ",         &none                      },

        {{0xD9, 0xF0},     "F2XM1",        &none                      },
        {{0xD9, 0xF1},     "FYL2X",        &none                      },
        {{0xD9, 0xF2},     "FPTAN",        &none                      },
        {{0xD9, 0xF3},     "FPATAN",       &none                      },
        {{0xD9, 0xF4},     "FXTRACT",      &none                      },
        {{0xD9, 0xF6},     "FDECSTP",      &none                      },
        {{0xD9, 0xF7},     "FINCSTP",      &none                      },
        {{0xD9, 0xF8},     "FPREM",        &none                      },
        {{0xD9, 0xF9},     "FYL2XP1",      &none                      },
        {{0xD9, 0xFA},     "FSQRT",        &none                      },
        {{0xD9, 0xFC},     "FRNDINT",      &none                      },
        {{0xD9, 0xFD},     "FSCALE",       &none                      },

        {0xDA,             "FIADD",   &R_RMDW,       OPExt::FPU_XY,  0},
        {0xDA,             "FIMUL",   &R_RMDW,       OPExt::FPU_XY,  1},
        {0xDA,             "FICOM",   &R_RMDW,       OPExt::FPU_XY,  2},
        {0xDA,             "FICOMP",  &R_RMDW,       OPExt::FPU_XY,  3},
        {0xDA,             "FISUB",   &R_RMDW,       OPExt::FPU_XY,  4},
        {0xDA,             "FISUBR",  &R_RMDW,       OPExt::FPU_XY,  5},
        {0xDA,             "FIDIV",   &R_RMDW,       OPExt::FPU_XY,  6},
        {0xDA,             "FIDIVR",  &R_RMDW,       OPExt::FPU_XY,  7},

        {0xDB,             "FILD",   &R_RMDW,        OPExt::FPU_XY,  0},
        {0xDB,             "FIST",   &R_RMDW,        OPExt::FPU_XY,  2},
        {0xDB,             "FISTP",  &R_RMDW,        OPExt::FPU_XY,  3},
        {0xDB,             "FLD",    &R_RMDW,        OPExt::FPU_XY,  5},
        {0xDB,             "FSTP",   &R_RMDW,        OPExt::FPU_XY,  7},

        {{0xDB, 0xE0},     "FENI",   &none                            },
        {{0xDB, 0xE1},     "FDISI",  &none                            },
        {{0xDB, 0xE2},     "FCLEX",  &none                            },
        {{0xDB, 0xE3},     "FINIT",  &none                            },
        {{0xDB, 0xE4},     "FSETPM", &none                            },

        {0xDC,             "FADD",   &R_RMQW,        OPExt::FPU_XY,  0},
        {0xDC,             "FADD",   &F_STREG_ST,    OPExt::FPU_11,  0},
        {0xDC,             "FMUL",   &R_RMQW,        OPExt::FPU_XY,  1},
        {0xDC,             "FMUL",   &F_STREG_ST,    OPExt::FPU_11,  1},
        {0xDC,             "FCOM",   &R_RMQW,        OPExt::FPU_XY,  2},
        {0xDC,             "FCOM",   &F_STREG,       OPExt::FPU_11,  2},
        {0xDC,             "FCOMP",  &R_RMQW,        OPExt::FPU_XY,  3},
        {0xDC,             "FCOMP",  &F_STREG,       OPExt::FPU_11,  3},
        {0xDC,             "FSUB",   &R_RMQW,        OPExt::FPU_XY,  4},
        {0xDC,             "FSUB",   &F_STREG_ST,    OPExt::FPU_11,  4},
        {0xDC,             "FSUBR",  &R_RMQW,        OPExt::FPU_XY,  5},
        {0xDC,             "FSUBR",  &F_STREG_ST,    OPExt::FPU_11,  5},
        {0xDC,             "FDIV",   &R_RMQW,        OPExt::FPU_XY,  6},
        {0xDC,             "FDIV",   &F_STREG_ST,    OPExt::FPU_11,  6},
        {0xDC,             "FDIVR",  &R_RMQW,        OPExt::FPU_XY,  7},
        {0xDC,             "FDIVR",  &F_STREG_ST,    OPExt::FPU_11,  7},
        
        {0xDD,             "FLD",     &R_RMQW,     OPExt::FPU_XY,  0},
//      {0xDD,             "FPU Reserved", &I_DB,       OPExt::FPU_XY,  1},
        {0xDD,             "FST",     &R_RMQW,     OPExt::FPU_XY,  2},
        {0xDD,             "FSTP",    &R_RMQW,     OPExt::FPU_XY,  3},
        {0xDD,             "FRSTOR",  &R_MEM,      OPExt::FPU_XY,  4},
//      {0xDD,             "FPU Reserved", &I_DB,       OPExt::FPU_XY,  5},
        {0xDD,             "FSAVE",   &R_MEM,      OPExt::FPU_XY,  6},
        {0xDD,             "FSTSW",   &R_RMQW,     OPExt::FPU_XY,  7},

        {0xDD,             "FFREE",   &F_STREG,    OPExt::FPU_11,  0},
        {0xDD,             "FXCH",    &F_STREG,    OPExt::FPU_11,  1},
        {0xDD,             "FST",     &F_STREG,    OPExt::FPU_11,  2},
        {0xDD,             "FSTP",    &F_STREG,    OPExt::FPU_11,  3},

        {0xDE,             "FIADD",   &R_RMW,        OPExt::FPU_XY,  0},
        {0xDE,             "FIMUL",   &R_RMW,        OPExt::FPU_XY,  1},
        {0xDE,             "FICOM",   &R_RMW,        OPExt::FPU_XY,  2},
        {0xDE,             "FICOMP",  &R_RMW,        OPExt::FPU_XY,  3},
        {0xDE,             "FISUB",   &R_RMW,        OPExt::FPU_XY,  4},
        {0xDE,             "FISUBR",  &R_RMW,        OPExt::FPU_XY,  5},
        {0xDE,             "FIDIV",   &R_RMW,        OPExt::FPU_XY,  6},
        {0xDE,             "FIDIVR",  &R_RMW,        OPExt::FPU_XY,  7},

        {0xDE,             "FADDP",   &F_STREG_ST,   OPExt::FPU_11,  0},
        {0xDE,             "FMULP",   &F_STREG_ST,   OPExt::FPU_11,  1},
        {0xDE,             "FCOMP",   &F_STREG_ST,   OPExt::FPU_11,  2},
        {{0xDE, 0xD9},     "FCOMPP",  &none                                },

        {0xDE,             "FSUBP",   &F_STREG_ST,   OPExt::FPU_11,  4},
        {0xDE,             "FSUBRP",  &F_STREG_ST,   OPExt::FPU_11,  5},
        {0xDE,             "FDIVP",   &F_STREG_ST,   OPExt::FPU_11,  6},
        {0xDE,             "FDIVRP",  &F_STREG_ST,   OPExt::FPU_11,  7},

        {0xDF,             "FILD",    &R_RMW,        OPExt::FPU_XY,  0},
//      {0xDF,             "F",       &R_RMW,        OPExt::FPU_XY,  1},
        {0xDF,             "FIST",    &R_RMW,        OPExt::FPU_XY,  2},
        {0xDF,             "FISTP",   &R_RMW,        OPExt::FPU_XY,  3},
        {0xDF,             "FBLD",    &R_RMB,        OPExt::FPU_XY,  4},
        {0xDF,             "FILD",    &R_RMQW,       OPExt::FPU_XY,  5},
        {0xDF,             "FBSTP",   &R_RMB,        OPExt::FPU_XY,  6},
        {0xDF,             "FISTP",   &R_RMQW,       OPExt::FPU_XY,  7},

        {0xDF,             "FFREEP",   &F_STREG,      OPExt::FPU_11,  0},
        {0xDF,             "FXCH",     &F_STREG,      OPExt::FPU_11,  1},
        {0xDF,             "FSTP",     &F_STREG,      OPExt::FPU_11,  2},
        {0xDF,             "FSTP",     &F_STREG,      OPExt::FPU_11,  3},
        {{0xDF, 0xe0},     "FSTSW AX", &none,                          },

};

// clang-format on

enum NumType { DEC, HEX1_NO_DECORATION, HEX1, HEX2, HEX4 };

struct Num {
    uint32_t val;
    enum NumType type;
};

struct Pad {
    size_t i;
};

// Note there is no zero termination required
class Line {
  public:
    char text[256];
    size_t len;

    Line &operator<<(const char *str) {
        const size_t strSize = strlen(str);
        const size_t rem = sizeof(text) - len;

        const size_t toAdd = std::min(strSize, rem);

        memcpy(text + len, str, toAdd);
        len += toAdd;

        return *this;
    }

    Line &operator<<(Pad pad) {
        if (pad.i > len && pad.i < sizeof(text)) {
            const size_t rm = pad.i - len;

            for (size_t i = 0; i < rm; i++) {
                text[len] = ' ';
                len++;
            }
        }

        return *this;
    }

    Line &operator<<(Num num) {
        char str[32]{};

        switch (num.type) {
        case DEC:
            snprintf(str, sizeof(str), "%d", num.val);
            break;
        case HEX1_NO_DECORATION:
            snprintf(str, sizeof(str), "%02X", num.val);
            break;
        case HEX1:
            snprintf(str, sizeof(str), "0x%02X", num.val);
            break;
        case HEX2:
            snprintf(str, sizeof(str), "0x%04X", num.val);
            break;
        case HEX4:
            snprintf(str, sizeof(str), "0x%08X", num.val);
            break;
        }

        return operator<<(str);
    }
};

static bool isRValidOrNone(const Description &description, size_t rem) {
    for (const D &d : description.d) {
        if (d.type == Type::RMB || d.type == Type::RMW ||
            d.type == Type::RMDW || d.type == Type::RMQW ||
            d.type == Type::RB || d.type == Type::RW || d.type == Type::MEM ||

            d.type == Type::SEG) {

            if (rem < 1) {
                return false;
            }

            break;
        }
    }

    return true;
}

static const Op *getOP(const uint8_t *cDecode, size_t rem) {
    for (const Op &op : ops) {

        if ((op.codeSz == 1 && rem < 1) || (op.codeSz == 2 && rem < 2)) {
            continue;
        }

        if (memcmp(op.code, cDecode, op.codeSz) != 0) {
            continue;
        }

        if (op.opExt == OPExt::N || op.opExt == OPExt::FPU_XY ||
            op.opExt == OPExt::FPU_11) {
            if (rem < op.codeSz + 1u) {
                continue;
            }

            const uint8_t b = cDecode[op.codeSz];
            const uint8_t n = (b >> 3) & 0b111;

            if (op.opExt == OPExt::FPU_XY) {
                const uint8_t mod = (b >> 6);

                if (mod == 0b11) {
                    continue;
                }
            } else if (op.opExt == OPExt::FPU_11) {
                const uint8_t mod = (b >> 6);

                if (mod != 0b11) {
                    continue;
                }
            }

            if (n != op.n) {
                continue;
            }

        } else if (!isRValidOrNone(*(op.description), rem - op.codeSz)) {
            continue;
        }

        return &op;
    }

    return nullptr;
}

static size_t getRMOffset(const Description &description,
                          const uint8_t *decode) {
    for (const D &d : description.d) {
        if (d.type == Type::RMB || d.type == Type::RMW ||
            d.type == Type::RMDW || d.type == Type::RMQW ||
            d.type == Type::MEM) {
            // That 1 byte is available is checked in getOP
            const uint8_t b = decode[0];

            const R_Type type = (R_Type)(b >> 6);
            const uint8_t rm = b & 0b111;

            return (size_t)getDispMemWidth(rm, type) + 1;
        } else if (d.type == Type::ST || d.type == Type::STREG) {
            return 1;
        }
    }

    return 0;
}

static size_t getLen(const Description &description, const uint8_t *decode) {
    size_t max = getRMOffset(description, decode);

    for (const D &d : description.d) {
        if (d.type == Type::DB) {
            max += 1;
        } else if (d.type == Type::DW || d.type == Type::DEREFBYTEATDW ||
                   d.type == Type::DEREFWORDATDW) {
            max += 2;
        } else if (d.type == Type::DDW) {
            max += 4;
        }
    }

    return max;
}

static void printRM(const uint8_t *cDecode, uint8_t rm, Width regWidth,
                    R_Type disp, Line &line) {
    if (disp == R_Type::NODISP && rm == 0b110) {
        const uint16_t num = cDecode[0] + (cDecode[1] << 8);
        line << getWidthName(regWidth) << " [" << Num{num, HEX2} << "]";

    } else if (disp == R_Type::REG) {
        if (regWidth == Width::BYTE) {
            line << rb[rm];
        } else {
            line << rw[rm];
        }
    } else {
        line << getWidthName(regWidth) << " [" << modNames[rm];

        const Width width = getDispMemWidth(rm, disp);

        if (width == Width::BYTE) {
            const uint16_t num = cDecode[0];
            line << " + " << Num{num, HEX1};
        } else if (width == Width::WORD) {
            const uint16_t num = cDecode[0] + (cDecode[1] << 8);
            line << " + " << Num{num, HEX2};
        }

        line << "]";
    }
}

static void printDescription(const Description &description,
                             const uint8_t *decode, Line &line) {
    size_t offset = getRMOffset(description, decode);
    bool first = true;

    for (const D &d : description.d) {
        if (d.type == Type::NONE) {
            continue;
        }

        if (first) {
            line << " ";
            first = false;
        } else {
            line << ", ";
        }

        if (d.type == Type::RMB) {
            // That 1 byte is available is checked in getOP
            const uint8_t b = decode[0];

            const R_Type type = (R_Type)(b >> 6);
            const uint8_t rm = b & 0b111;

            printRM(decode + 1, rm, Width::BYTE, type, line);
        } else if (d.type == Type::RMW) {
            // That 1 byte is available is checked in getOP
            const uint8_t b = decode[0];

            const R_Type type = (R_Type)(b >> 6);
            const uint8_t rm = b & 0b111;

            printRM(decode + 1, rm, Width::WORD, type, line);
        } else if (d.type == Type::RMDW) {
            // That 1 byte is available is checked in getOP
            const uint8_t b = decode[0];

            const R_Type type = (R_Type)(b >> 6);
            const uint8_t rm = b & 0b111;

            printRM(decode + 1, rm, Width::DWORD, type, line);
        } else if (d.type == Type::RMQW) {
            // That 1 byte is available is checked in getOP
            const uint8_t b = decode[0];

            const R_Type type = (R_Type)(b >> 6);
            const uint8_t rm = b & 0b111;

            printRM(decode + 1, rm, Width::QWORD, type, line);
        } else if (d.type == Type::MEM) {
            // That 1 byte is available is checked in getOP
            const uint8_t b = decode[0];

            const R_Type type = (R_Type)(b >> 6);
            const uint8_t rm = b & 0b111;

            printRM(decode + 1, rm, Width::NONE, type, line);
        } else if (d.type == Type::DB) {
            line << "BYTE " << Num{decode[offset], HEX1};
            offset++;
        } else if (d.type == Type::DW) {
            const uint16_t num = (decode[offset]) + (decode[offset + 1] << 8);

            line << "WORD " << Num{num, HEX2};
            offset += 2;
        } else if (d.type == Type::DEREFBYTEATDW) {
            const uint16_t num = (decode[offset]) + (decode[offset + 1] << 8);

            line << "BYTE [" << Num{num, HEX2} << "]";
            offset += 2;
        } else if (d.type == Type::DEREFWORDATDW) {
            const uint16_t num = (decode[offset]) + (decode[offset + 1] << 8);

            line << "WORD [" << Num{num, HEX2} << "]";
            offset += 2;
        } else if (d.type == Type::RB) {
            const uint8_t b = decode[0];
            const uint8_t r = (b >> 3) & 0b111;

            line << rb[r];
        } else if (d.type == Type::RW) {
            const uint8_t b = decode[0];
            const uint8_t r = (b >> 3) & 0b111;

            line << rw[r];
        } else if (d.type == Type::SEG) {
            const uint8_t b = decode[0];
            const uint8_t seg = (b >> 3) & 0b111;

            if (seg < (uint8_t)Segment::END) {
                line << segments[seg];
            } else {
                line << "?";
            }

        } else if (d.type == Type::CONSTBYTE) {
            line << Num{d.num, DEC};
        } else if (d.type == Type::CSEG) {
            line << segments[d.num];
        } else if (d.type == Type::DDW) {
            const uint32_t num = (decode[offset]) + (decode[offset + 1] << 8) +
                                 (decode[offset + 2] << 16) +
                                 (decode[offset + 3] << 24);

            line << "DWORD " << Num{num, HEX4};

            offset += 4;
        } else if (d.type == Type::REGB) {
            line << rb[d.num];
        } else if (d.type == Type::REGW) {
            line << rw[d.num];
        } else if (d.type == Type::ST) {
            line << "ST";
        } else if (d.type == Type::STREG) {
            const uint8_t b = decode[0];
            const uint8_t reg = b & 0b111;

            line << "ST" << Num{reg, DEC};
        }
    }
}

static size_t printOP(const std::vector<uint8_t> &decode, uint32_t decodeOffset,
                      uint32_t execOffset, const Op &op, Line &line) {

    const size_t rem = decode.size() - (decodeOffset + op.codeSz);

    const size_t len =
        getLen(*(op.description), decode.data() + decodeOffset + op.codeSz);

    if (len > rem) {
        line << Num{execOffset + decodeOffset, HEX4} << ":  ";

        for (size_t i = 0; i < rem; i++) {
            line << Num{*(decode.data() + decodeOffset + i), HEX1_NO_DECORATION}
                 << " ";
        }

        if (rem) {
            line << "; " << Pad{36} << "DB ";
            line << Num{*(decode.data() + decodeOffset + 0), HEX1};
        }

        for (size_t i = 1; i < rem; i++) {
            line << ", " << Num{*(decode.data() + decodeOffset + i), HEX1};
        }

        return decodeOffset + len;
    }

    line << Num{execOffset + decodeOffset, HEX4} << ":  ";

    for (size_t i = 0; i < len + op.codeSz; i++) {
        line << Num{*(decode.data() + decodeOffset + i), HEX1_NO_DECORATION}
             << " ";
    }

    line << "; " << Pad{36} << op.name << Pad{50};

    printDescription(*(op.description),
                     decode.data() + decodeOffset + op.codeSz, line);

    return decodeOffset + len + op.codeSz;
}

static void dec(const std::vector<uint8_t> &decode, uint32_t execOffset) {
    uint32_t decodeOffset = 0;

    while (decodeOffset < decode.size()) {
        const Op *op =
            getOP(decode.data() + decodeOffset, decode.size() - decodeOffset);

        Line line{};

        if (op) {
            decodeOffset = printOP(decode, decodeOffset, execOffset, *op, line);
        } else {
            const uint8_t op1 = *(decode.data() + decodeOffset);
            const size_t rem = decode.size() - decodeOffset;

            if (rem >= 2 && op1 >= 0xD8 && op1 <= 0xDF) {
                // All reserved FPU instructions which have a fixed size (plus
                // disp depending on mod)

                const uint8_t op2 = *(decode.data() + decodeOffset + 1);

                line << Num{*(decode.data() + decodeOffset), HEX4} << ":  "
                     << Num{op1, HEX1_NO_DECORATION} << " "
                     << Num{op2, HEX1_NO_DECORATION};

                const uint8_t mod = op2 >> 6;

                if (mod == 0b01 && rem >= 3) {
                    const uint8_t d1 = *(decode.data() + decodeOffset + 2);
                    line << " " << Num{d1, HEX1_NO_DECORATION};
                    decodeOffset += 3;
                } else if (mod == 0b10 && rem >= 4) {
                    const uint8_t d1 = *(decode.data() + decodeOffset + 2);
                    const uint8_t d2 = *(decode.data() + decodeOffset + 3);

                    line << " " << Num{d1, HEX1_NO_DECORATION};
                    line << " " << Num{d2, HEX1_NO_DECORATION};
                    decodeOffset += 4;
                } else {
                    decodeOffset += 2;
                }

                line << " ; " << Pad{36} << "FPU RESERVED";

            } else {
                // Move one byte and maybe we will sync (doubtful)
                line << Num{*(decode.data() + decodeOffset), HEX4} << ":  "
                     << Num{op1, HEX1_NO_DECORATION} << " ; " << Pad{36}
                     << "DB " << Num{op1, HEX1};
                decodeOffset++;
            }
        }

        printf("%.*s\n", (int)line.len, line.text);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 1) {
        printf("Shell error\n");
        return -1;
    }

    if (argc < 2) {
        printf("Use %s filename [offset]\n", argv[0]);
        return -1;
    }

    const char *filename = argv[1];

    size_t execOffset = 0x100;

    if (argc == 3) {
        char *endptr;
        execOffset = strtol(argv[2], &endptr, 16);

        if (*endptr != '\0') {
            printf("Argument offset is not a hexidecimal number");
            return -1;
        }
    }

    try {
        const FileDescriptorRO rofd(filename);
        dec(getBuffer(rofd.fd), execOffset);
    } catch (...) {
        printf("Exception\n");
        return -3;
    }

    return 0;
}
