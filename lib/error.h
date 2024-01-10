// SPDX-License-Identifier: Apache-2.0
/**
 * Copyright (c) 2023  Panasonic Automotive Systems, Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __error_h__
#define __error_h__

#include <stdio.h>

#define error(_format, ...) ({                       \
	fprintf(stderr, "* %s():%d : " _format "\n", \
		__func__, __LINE__, ##__VA_ARGS__); })

#define error_errno(_format, ...) ({                    \
	fprintf(stderr, "* %s():%d %m : " _format "\n", \
		__func__, __LINE__, ##__VA_ARGS__); })

#ifdef NDEBUG
/*
 * This is how trace() and trace("") both could be possible
 * in Release builds (NDEBUG is defined)
 */
# define trace(_format, ...) do {} while (0)
#else
# define trace(_format, ...) ({                      \
	fprintf(stderr, "- %s():%d : " _format "\n", \
		__func__, __LINE__, ##__VA_ARGS__); })
#endif

#endif /* TRACE_H */
