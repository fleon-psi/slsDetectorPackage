// SPDX-License-Identifier: LGPL-3.0-or-other
// Copyright (C) 2021 Contributors to the SLS Detector Package
#pragma once

#include "sls/md5.h"
#include <stdint.h> // int64_t
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

enum numberMode { DEC, HEX };

/**
 * Convert a value from a range to a different range (eg voltage to dac or vice
 * versa)
 * @param inputMin input minimum
 * @param inputMax input maximum
 * @param outputMin output minimum
 * @param outputMax output maximum
 * @param inputValue input value
 * @param outputValue pointer to output value
 * @returns FAIL if input value is out of bounds, else OK
 */
int ConvertToDifferentRange(int inputMin, int inputMax, int outputMin,
                            int outputMax, int inputValue, int *outputValue);

int getAbsPath(char *buf, size_t bufSize, char *fname);

int getTimeFromString(char *buf, time_t *result);

int getKernelVersion(char* retvals);

int validateKernelVersion(char *expectedVersion);

void validate(int *ret, char *mess, int arg, int retval, char *modename,
              enum numberMode nummode);
void validate64(int *ret, char *mess, int64_t arg, int64_t retval,
                char *modename, enum numberMode nummode);

int getModuleIdInFile(int *ret, char *mess, char *fileName);
int verifyChecksumFromBuffer(char *mess, char *clientChecksum, char *buffer,
                             ssize_t bytes);
int verifyChecksumFromFile(char *mess, char *clientChecksum, char *fname);
int verifyChecksumFromFlash(char *mess, char *clientChecksum, char *fname,
                            ssize_t fsize);
int verifyChecksum(char *mess, char *clientChecksum, MD5_CTX *c, char *msg);