;
; EnlyzeWinCompatLib - Let Clang-compiled applications run on older Windows versions
; Copyright (c) 2021 Colin Finck, ENLYZE GmbH <c.finck@enlyze.com>
; SPDX-License-Identifier: MIT
;

.model flat

EXTERN _LibFlsAlloc@4 : PROC
EXTERN _LibFlsFree@4 : PROC
EXTERN _LibFlsSetValue@8 : PROC
EXTERN _LibFlsGetValue@4 : PROC
EXTERN _LibIsThreadAFiber@0 : PROC
EXTERN _LibConvertFiberToThread@0 : PROC
EXTERN _LibConvertThreadToFiber@4 : PROC
EXTERN _LibConvertThreadToFiberEx@8 : PROC
EXTERN _LibSwitchToFiber@4 : PROC
EXTERN _LibDeleteFiber@4 : PROC
EXTERN _LibExitThread@4 : PROC
EXTERN _LibExitProcess@4 : PROC
EXTERN _LibFreeLibraryAndExitThread@8 : PROC

.data

PUBLIC __imp__FlsAlloc@4
__imp__FlsAlloc@4 dd _LibFlsAlloc@4

PUBLIC __imp__FlsFree@4
__imp__FlsFree@4 dd _LibFlsFree@4

PUBLIC __imp__FlsSetValue@8
__imp__FlsSetValue@8 dd _LibFlsSetValue@8

PUBLIC __imp__FlsGetValue@4
__imp__FlsGetValue@4 dd _LibFlsGetValue@4

PUBLIC __imp__IsThreadAFiber@0
__imp__IsThreadAFiber@0 dd _LibIsThreadAFiber@0

PUBLIC __imp__ConvertFiberToThread@0
__imp__ConvertFiberToThread@0 dd _LibConvertFiberToThread@0

PUBLIC __imp__ConvertThreadToFiber@4
__imp__ConvertThreadToFiber@4 dd _LibConvertThreadToFiber@4

PUBLIC __imp__ConvertThreadToFiberEx@8
__imp__ConvertThreadToFiberEx@8 dd _LibConvertThreadToFiberEx@8

PUBLIC __imp__SwitchToFiber@4
__imp__SwitchToFiber@4 dd _LibSwitchToFiber@4

PUBLIC __imp__DeleteFiber@4
__imp__DeleteFiber@4 dd _LibDeleteFiber@4

PUBLIC __imp__ExitThread@4
__imp__ExitThread@4 dd _LibExitThread@4

PUBLIC __imp__ExitProcess@4
__imp__ExitProcess@4 dd _LibExitProcess@4

PUBLIC __imp__FreeLibraryAndExitThread@8
__imp__FreeLibraryAndExitThread@8 dd _LibFreeLibraryAndExitThread@8

END
