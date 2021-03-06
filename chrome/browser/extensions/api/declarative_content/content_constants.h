// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Constants used for the declarativeContent API.

#ifndef CHROME_BROWSER_EXTENSIONS_API_DECLARATIVE_CONTENT_CONTENT_CONSTANTS_H_
#define CHROME_BROWSER_EXTENSIONS_API_DECLARATIVE_CONTENT_CONTENT_CONSTANTS_H_

namespace extensions {
namespace declarative_content_constants {

// Signals to which ContentRulesRegistries are registered.
extern const char kOnPageChanged[];

// Keys of dictionaries.
extern const char kCss[];
extern const char kInstanceType[];
extern const char kPageUrl[];

// Values of dictionaries, in particular instance types
extern const char kPageStateMatcherType[];
extern const char kShowPageAction[];

}  // namespace declarative_content_constants
}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_DECLARATIVE_CONTENT_CONTENT_CONSTANTS_H_
