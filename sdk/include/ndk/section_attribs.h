/*++ NDK Version: 0099

Copyright (c) Alex Ionescu.  All rights reserved.

Header Name:

    section_attribs.h

Abstract:

    Preprocessor definitions to put code and data into specific sections.

Author:

    Timo Kreuzer (timo.kreuzer@reactos.org)

--*/

#pragma once

#if defined(__GNUC__) || defined(__clang__)

  #define INIT_FUNCTION __attribute__((section ("INIT")))

  #define DATA_SEG(segment) __attribute__((section(segment)))
  #define CODE_SEG(segment) __attribute__((section(segment)))

#elif defined(_MSC_VER)

  #pragma comment(linker, "/SECTION:INIT,ERW")

  #if (_MSC_VER >= 1800) // Visual Studio 2013 / version 12.0
    #define INIT_FUNCTION __declspec(code_seg("INIT"))
  #else
    #pragma section("INIT", read,execute,discard)
    #define INIT_FUNCTION
  #endif

  #define DATA_SEG(segment) __declspec(allocate(segment))
  #define CODE_SEG(segment) __declspec(code_seg(segment))

#else
  #error Invalid compiler!
#endif
