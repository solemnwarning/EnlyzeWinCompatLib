;
; EnlyzeWinCompatLib - Let Clang-compiled applications run on older Windows versions
; Copyright (c) 2021 Colin Finck, ENLYZE GmbH <c.finck@enlyze.com>
; SPDX-License-Identifier: MIT
;

.model flat

EXTERN _LibInitializeCriticalSectionEx@12 : PROC
EXTERN _LibGetLocaleInfoEx@16 : PROC

.data

PUBLIC __imp__InitializeCriticalSectionEx@12
__imp__InitializeCriticalSectionEx@12 dd _LibInitializeCriticalSectionEx@12

PUBLIC __imp__GetLocaleInfoEx@16
__imp__GetLocaleInfoEx@16 dd _LibGetLocaleInfoEx@16

END
