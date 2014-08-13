/*
** Copyright (C) 2014 Cisco and/or its affiliates. All rights reserved.
 ** Copyright (C) 2002-2013 Sourcefire, Inc.
 ** Author: Martin Roesch
 **
 ** This program is free software; you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License Version 2 as
 ** published by the Free Software Foundation.  You may not use, modify or
 ** distribute this program under any other version of the GNU General
 ** Public License.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program; if not, write to the Free Software
 ** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/* byte_test
 *
 * Purpose:
 *      Test a byte field against a specific value (with opcode).  Capable
 *      of testing binary values or converting represenative byte strings
 *      to their binary equivalent and testing them.
 *
 *
 * Arguments:
 *      Required:
 *      <bytes_to_convert>: number of bytes to pick up from the packet
 *      <opcode>: operation to perform to test the value (<,>,=,!)
 *      <value>: value to test the converted value against
 *      <offset>: number of bytes into the payload to start processing
 *      Optional:
 *      ["relative"]: offset relative to last pattern match
 *      ["big"]: process data as big endian (default)
 *      ["little"]: process data as little endian
 *      ["string"]: converted bytes represented as a string needing conversion
 *      ["hex"]: converted string data is represented in hexidecimal
 *      ["dec"]: converted string data is represented in decimal
 *      ["oct"]: converted string data is represented in octal
 *
 *   sample rules:
 *   alert udp $EXTERNAL_NET any -> $HOME_NET any \
 *      (msg:"AMD procedure 7 plog overflow "; \
 *      content: "|00 04 93 F3|"; \
 *      content: "|00 00 00 07|"; distance: 4; within: 4; \
 *      byte_test: 4,>, 1000, 20, relative;)
 *
 *   alert tcp $EXTERNAL_NET any -> $HOME_NET any \
 *      (msg:"AMD procedure 7 plog overflow "; \
 *      content: "|00 04 93 F3|"; \
 *      content: "|00 00 00 07|"; distance: 4; within: 4; \
 *      byte_test: 4, >,1000, 20, relative;)
 *
 * alert udp any any -> any 1234 \
 *      (byte_test: 4, =, 1234, 0, string, dec; \
 *      msg: "got 1234!";)
 *
 * alert udp any any -> any 1235 \
 *      (byte_test: 3, =, 123, 0, string, dec; \
 *      msg: "got 123!";)
 *
 * alert udp any any -> any 1236 \
 *      (byte_test: 2, =, 12, 0, string, dec; \
 *      msg: "got 12!";)
 *
 * alert udp any any -> any 1237 \
 *      (byte_test: 10, =, 1234567890, 0, string, dec; \
 *      msg: "got 1234567890!";)
 *
 * alert udp any any -> any 1238 \
 *      (byte_test: 8, =, 0xdeadbeef, 0, string, hex; \
 *      msg: "got DEADBEEF!";)
 *
 * Effect:
 *
 *      Reads in the indicated bytes, converts them to an numeric
 *      representation and then performs the indicated operation/test on
 *      the data using the value field.  Returns 1 if the operation is true,
 *      0 if it is not.
 */

#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include <string>
using namespace std;

#include "snort_types.h"
#include "snort_bounds.h"
#include "extract.h"
#include "detection/treenodes.h"
#include "protocols/packet.h"
#include "parser.h"
#include "snort_debug.h"
#include "util.h"
#include "mstring.h"
#include "sfhashfcn.h"
#include "ips_byte_extract.h"
#include "snort.h"
#include "profiler.h"
#include "sfhashfcn.h"
#include "detection/detection_defines.h"
#include "detection/detection_util.h"
#include "fpdetect.h"
#include "framework/cursor.h"
#include "framework/ips_option.h"
#include "framework/parameter.h"
#include "framework/module.h"

#define PARSELEN 10
#define TEXTLEN  (PARSELEN + 2)

static THREAD_LOCAL ProfileStats byteTestPerfStats;

static const char* s_name = "byte_test";

// FIXIT cloned from sf_snort_plugin_api.h
#define CHECK_EQ            0
#define CHECK_NEQ           1
#define CHECK_LT            2
#define CHECK_GT            3
#define CHECK_LTE           4
#define CHECK_GTE           5
#define CHECK_AND           6
#define CHECK_XOR           7
#define CHECK_ALL           8
#define CHECK_ATLEASTONE    9
#define CHECK_NONE          10

#define BT_LESS_THAN            CHECK_LT
#define BT_EQUALS               CHECK_EQ
#define BT_GREATER_THAN         CHECK_GT
#define BT_AND                  CHECK_AND
#define BT_XOR                  CHECK_XOR
#define BT_GREATER_THAN_EQUAL   CHECK_GTE
#define BT_LESS_THAN_EQUAL      CHECK_LTE
#define BT_CHECK_ALL            CHECK_ALL
#define BT_CHECK_ATLEASTONE     CHECK_ATLEASTONE
#define BT_CHECK_NONE           CHECK_NONE

#define BIG    0
#define LITTLE 1

typedef struct _ByteTestData
{
    uint32_t bytes_to_compare;
    uint32_t cmp_value;
    uint32_t opcode;
    int32_t offset;
    uint8_t not_flag;
    uint8_t relative_flag;
    uint8_t data_string_convert_flag;
    int8_t endianess;
    uint32_t base;
    int8_t cmp_value_var;
    int8_t offset_var;
} ByteTestData;

class ByteTestOption : public IpsOption
{
public:
    ByteTestOption(const ByteTestData& c) : IpsOption(s_name)
    { config = c; };

    ~ByteTestOption() { };

    uint32_t hash() const;
    bool operator==(const IpsOption&) const;

    bool is_relative()
    { return ( config.relative_flag == 1 ); };

    int eval(Cursor&, Packet*);

private:
    ByteTestData config;
};

//-------------------------------------------------------------------------
// class methods
//-------------------------------------------------------------------------

uint32_t ByteTestOption::hash() const
{
    uint32_t a,b,c;
    const ByteTestData *data = (ByteTestData *)&config;

    a = data->bytes_to_compare;
    b = data->cmp_value;
    c = data->opcode;

    mix(a,b,c);

    a += data->offset;
    b += (data->not_flag << 24 |
          data->relative_flag << 16 |
          data->data_string_convert_flag << 8 |
          data->endianess);
    c += data->base;

    mix(a,b,c);

    a += data->cmp_value_var;
    b += data->offset_var;

    mix(a,b,c);
    mix_str(a,b,c,get_name());
    final(a,b,c);

    return c;
}

bool ByteTestOption::operator==(const IpsOption& ips) const
{
    if ( strcmp(s_name, ips.get_name()) )
        return false;

    ByteTestOption& rhs = (ByteTestOption&)ips;
    const ByteTestData *left = &config;
    const ByteTestData *right = &rhs.config;

    if (( left->bytes_to_compare == right->bytes_to_compare) &&
        ( left->cmp_value == right->cmp_value) &&
        ( left->opcode == right->opcode) &&
        ( left->offset == right->offset) &&
        ( left->not_flag == right->not_flag) &&
        ( left->relative_flag == right->relative_flag) &&
        ( left->data_string_convert_flag == right->data_string_convert_flag) &&
        ( left->endianess == right->endianess) &&
        ( left->base == right->base) &&
        ( left->cmp_value_var == right->cmp_value_var) &&
        ( left->offset_var == right->offset_var))
    {
        return true;
    }

    return false;
}

int ByteTestOption::eval(Cursor& c, Packet*)
{
    ByteTestData *btd = (ByteTestData *)&config;
    int rval = DETECTION_OPTION_NO_MATCH;
    uint32_t value = 0;
    int success = 0;
    const uint8_t *base_ptr, *end_ptr, *start_ptr;
    int payload_bytes_grabbed;
    int offset;
    uint32_t cmp_value;

    PROFILE_VARS;
    MODULE_PROFILE_START(byteTestPerfStats);

    /* Get values from byte_extract variables, if present. */
    if (btd->cmp_value_var >= 0 && btd->cmp_value_var < NUM_BYTE_EXTRACT_VARS)
    {
        uint32_t val;
        GetByteExtractValue(&val, btd->cmp_value_var);
        cmp_value = val;
    }
    else
        cmp_value = btd->cmp_value;

    if (btd->offset_var >= 0 && btd->offset_var < NUM_BYTE_EXTRACT_VARS)
    {
        uint32_t val;
        GetByteExtractValue(&val, btd->offset_var);
        offset = (int32_t)val;
    }
    else
        offset = btd->offset;

    if ( btd->relative_flag )
    {
        start_ptr = c.start();
        end_ptr = start_ptr + c.length();
    }
    else
    {
        start_ptr = c.buffer();
        end_ptr = start_ptr + c.size();
    }
    base_ptr = start_ptr + offset;

    /* both of these functions below perform their own bounds checking within
     * byte_extract.c
     */

    if(!btd->data_string_convert_flag)
    {
        if ( byte_extract(
            btd->endianess, btd->bytes_to_compare,
            (const uint8_t *)base_ptr,
            (const uint8_t *)start_ptr,
            (const uint8_t *)end_ptr,
            &value))
        {
            MODULE_PROFILE_END(byteTestPerfStats);
            return rval;
        }
        payload_bytes_grabbed = (int)btd->bytes_to_compare;
    }
    else
    {
        payload_bytes_grabbed = string_extract(
            btd->bytes_to_compare, btd->base,
            (const uint8_t *)base_ptr,
            (const uint8_t *)start_ptr,
            (const uint8_t *)end_ptr,
            &value);

        if ( payload_bytes_grabbed < 0 )
        {
            DEBUG_WRAP(DebugMessage(DEBUG_PATTERN_MATCH,
                                    "String Extraction Failed\n"););

            MODULE_PROFILE_END(byteTestPerfStats);
            return rval;
        }
    }

    DEBUG_WRAP(DebugMessage(DEBUG_PATTERN_MATCH,
        "Grabbed %d bytes at offset %d, value = 0x%08X(%u)\n",
        payload_bytes_grabbed, btd->offset, value, value); );

    switch(btd->opcode)
    {
        case BT_LESS_THAN: if(value < cmp_value)
                               success = 1;
                           break;

        case BT_EQUALS: if(value == cmp_value)
                            success = 1;
                        break;

        case BT_GREATER_THAN: if(value > cmp_value)
                                  success = 1;
                              break;

        case BT_AND: if ((value & cmp_value) > 0)
                         success = 1;
                     break;

        case BT_XOR: if ((value ^ cmp_value) > 0)
                        success = 1;
                    break;

        case BT_GREATER_THAN_EQUAL: if (value >= cmp_value)
                                        success = 1;
                                    break;

        case BT_LESS_THAN_EQUAL: if (value <= cmp_value)
                                        success = 1;
                                 break;

        case BT_CHECK_ALL: if ((value & cmp_value) == cmp_value)
                               success = 1;
                           break;

        case BT_CHECK_ATLEASTONE: if ((value & cmp_value) != 0)
                                      success = 1;
                                  break;

        case BT_CHECK_NONE: if ((value & cmp_value) == 0)
                                success = 1;
                            break;
    }

    if (btd->not_flag)
    {
        DEBUG_WRAP(DebugMessage(DEBUG_PATTERN_MATCH,
                    "checking for not success...flag\n"););
        if (!success)
        {
            rval = DETECTION_OPTION_MATCH;
        }
    }
    else if (success)
    {
        rval = DETECTION_OPTION_MATCH;
    }

    /* if the test isn't successful, this function *must* return 0 */
    MODULE_PROFILE_END(byteTestPerfStats);
    return rval;
}

//-------------------------------------------------------------------------
// api
//-------------------------------------------------------------------------

static void parse_operator(const char* cptr, ByteTestData& idx)
{
    while(isspace((int)*cptr)) {cptr++;}

    if(*cptr == '!')
    {
       idx.not_flag = 1;
       cptr++;
    }

    if (idx.not_flag && strlen(cptr) == 0)
    {
        idx.opcode = BT_EQUALS;
    }
    else
    {
        /* set the opcode */
        switch(*cptr)
        {
            case '<': idx.opcode = BT_LESS_THAN;
                      cptr++;
                      if (*cptr == '=')
                          idx.opcode = BT_LESS_THAN_EQUAL;
                      else
                          cptr--;
                      break;

            case '=': idx.opcode = BT_EQUALS;
                      break;

            case '>': idx.opcode = BT_GREATER_THAN;
                      cptr++;
                      if (*cptr == '=')
                          idx.opcode = BT_GREATER_THAN_EQUAL;
                      else
                          cptr--;
                      break;

            case '&': idx.opcode = BT_AND;
                      break;

            case '^': idx.opcode = BT_XOR;
                      break;

            default: ParseError(
                "byte_test unknown opcode ('%c)", *cptr);
        }
    }
}

//-------------------------------------------------------------------------
// module
//-------------------------------------------------------------------------

static const Parameter jump_params[] =
{
    { "~count", Parameter::PT_INT, "1:10", nullptr,
      "number of bytes to pick up from the buffer" },

    { "~operator", Parameter::PT_STRING, nullptr, nullptr,
      "variable name or number of bytes into the buffer to start processing" },

    { "~compare", Parameter::PT_STRING, nullptr, nullptr,
      "variable name or value to test the converted result against" },

    { "~offset", Parameter::PT_STRING, nullptr, nullptr,
      "variable name or number of bytes into the payload to start processing" },

    { "relative", Parameter::PT_IMPLIED, nullptr, nullptr,
      "offset from cursor instead of start of buffer" },

    { "big", Parameter::PT_IMPLIED, nullptr, nullptr,
      "big endian" },

    { "little", Parameter::PT_IMPLIED, nullptr, nullptr,
      "little endian" },

    { "dce", Parameter::PT_IMPLIED, nullptr, nullptr,
      "dcerpc2 determines endianness" },

    { "string", Parameter::PT_IMPLIED, nullptr, nullptr,
      "convert from string" },

    { "hex", Parameter::PT_IMPLIED, nullptr, nullptr,
      "convert from hex string" },

    { "oct", Parameter::PT_IMPLIED, nullptr, nullptr,
      "convert from octal string" },

    { "dec", Parameter::PT_IMPLIED, nullptr, nullptr,
      "convert from decimal string" },

    { nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr }
};

class ByteTestModule : public Module
{
public:
    ByteTestModule() : Module(s_name, jump_params) { };

    bool begin(const char*, int, SnortConfig*);
    bool end(const char*, int, SnortConfig*);
    bool set(const char*, Value&, SnortConfig*);

    ProfileStats* get_profile() const
    { return &byteTestPerfStats; };

    ByteTestData data;
    string cmp_var;
    string off_var;
};

bool ByteTestModule::begin(const char*, int, SnortConfig*)
{
    memset(&data, 0, sizeof(data));
    cmp_var.clear();
    off_var.clear();
    return true;
}

bool ByteTestModule::end(const char*, int, SnortConfig*)
{
    if ( off_var.size() )
    {
        data.offset_var = GetVarByName(off_var.c_str());

        if (data.offset_var == BYTE_EXTRACT_NO_VAR)
        {
            ParseError("%s", BYTE_EXTRACT_INVALID_ERR_STR);
            return false;
        }
    }
    if ( cmp_var.size() )
    {
        data.cmp_value_var = GetVarByName(cmp_var.c_str());

        if (data.cmp_value_var == BYTE_EXTRACT_NO_VAR)
        {
            ParseError("%s", BYTE_EXTRACT_INVALID_ERR_STR);
            return false;
        }
    }
    unsigned e1 = ffs(data.endianess);
    unsigned e2 = ffs(data.endianess >> e1);
    
    if ( e1 && e2 )
    {
        ParseError("byte_extract rule option has multiple arguments "
            "specifying the type of string conversion. Use only "
            "one of 'dec', 'hex', or 'oct'.");
        return false;
    }
    return true;
}

bool ByteTestModule::set(const char*, Value& v, SnortConfig*)
{
    if ( v.is("~count") )
        data.bytes_to_compare = v.get_long();

    else if ( v.is("~operator") )
        parse_operator(v.get_string(), data);

    else if ( v.is("~compare") )
    {
        long n;
        if ( v.strtol(n) )
            data.offset = n;
        else
            cmp_var = v.get_string();
    }
    else if ( v.is("~offset") )
    {
        long n;
        if ( v.strtol(n) )
            data.offset = n;
        else
            off_var = v.get_string();
    }
    else if ( v.is("relative") )
        data.relative_flag = 1;

    else if ( v.is("big") )
        data.endianess |= ENDIAN_LITTLE;

    else if ( v.is("little") )
        data.endianess |= ENDIAN_BIG;

    else if ( v.is("dce") )
        data.endianess |= ENDIAN_FUNC;

    else if ( v.is("string") )
    {
        data.data_string_convert_flag = 1;
        data.base = 10;
    }
    else if ( v.is("dec") )
        data.base = 10;

    else if ( v.is("hex") )
        data.base = 16;

    else if ( v.is("oct") )
        data.base = 8;

    else
        return false;

    return true;
}

//-------------------------------------------------------------------------
// api methods
//-------------------------------------------------------------------------

static Module* mod_ctor()
{
    return new ByteTestModule;
}

static void mod_dtor(Module* m)
{
    delete m;
}

static IpsOption* byte_test_ctor(Module* p, OptTreeNode*)
{
    ByteTestModule* m = (ByteTestModule*)p;
    return new ByteTestOption(m->data);
}

static void byte_test_dtor(IpsOption* p)
{
    delete p;
}

static const IpsApi byte_test_api =
{
    {
        PT_IPS_OPTION,
        s_name,
        IPSAPI_PLUGIN_V0,
        0,
        mod_ctor,
        mod_dtor
    },
    OPT_TYPE_DETECTION,
    0, 0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    byte_test_ctor,
    byte_test_dtor,
    nullptr
};

#ifdef BUILDING_SO
SO_PUBLIC const BaseApi* snort_plugins[] =
{
    &byte_test_api.base,
    nullptr
};
#else
const BaseApi* ips_byte_test = &byte_test_api.base;
#endif

