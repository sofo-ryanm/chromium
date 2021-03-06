// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SEARCH_SEARCH_UI_H_
#define CHROME_BROWSER_UI_SEARCH_SEARCH_UI_H_

#include "third_party/skia/include/core/SkColor.h"

namespace ui {
class ThemeProvider;
}

namespace chrome {

// Returns the color to use to draw the detached bookmark bar background.
SkColor GetDetachedBookmarkBarBackgroundColor(
    ui::ThemeProvider* theme_provider);

// Returns the color to use to draw the detached bookmark bar separator.
SkColor GetDetachedBookmarkBarSeparatorColor(ui::ThemeProvider* theme_provider);

}  // namespace chrome

#endif  // CHROME_BROWSER_UI_SEARCH_SEARCH_UI_H_
