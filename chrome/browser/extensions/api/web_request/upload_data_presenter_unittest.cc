// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/web_request/upload_data_presenter.h"
#include "chrome/browser/extensions/api/web_request/web_request_api_constants.h"
#include "net/base/upload_element.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::BinaryValue;
using base::ListValue;
using base::StringValue;
using base::Value;

namespace keys = extension_web_request_api_constants;

namespace extensions {

// This only tests the handling of dots in keys. Other functionality is covered
// by ExtensionWebRequestTest.AccessRequestBodyData and
// WebRequestFormDataParserTest.
TEST(WebRequestUploadDataPresenterTest, ParsedData) {
  // Input.
  const char block[] = "key.with.dots=value";
  net::UploadElement element;
  element.SetToBytes(block, sizeof(block) - 1);

  // Expected output.
  scoped_ptr<ListValue> values(new ListValue);
  values->Append(Value::CreateStringValue("value"));
  DictionaryValue expected_form;
  expected_form.SetWithoutPathExpansion("key.with.dots", values.release());

  // Real output.
  scoped_ptr<ParsedDataPresenter> parsed_data_presenter(
      ParsedDataPresenter::CreateForTests());
  ASSERT_TRUE(parsed_data_presenter.get() != NULL);
  parsed_data_presenter->FeedNext(element);
  EXPECT_TRUE(parsed_data_presenter->Succeeded());
  scoped_ptr<Value> result = parsed_data_presenter->Result();
  ASSERT_TRUE(result.get() != NULL);

  EXPECT_TRUE(result->Equals(&expected_form));
}

TEST(WebRequestUploadDataPresenterTest, RawData) {
  // Input.
  const char block1[] = "test";
  const size_t block1_size = sizeof(block1) - 1;
  const char kFilename[] = "path/test_filename.ext";
  const char block2[] = "another test";
  const size_t block2_size = sizeof(block2) - 1;

  // Expected output.
  scoped_ptr<BinaryValue> expected_a(
      BinaryValue::CreateWithCopiedBuffer(block1, block1_size));
  ASSERT_TRUE(expected_a.get() != NULL);
  scoped_ptr<StringValue> expected_b(Value::CreateStringValue(kFilename));
  ASSERT_TRUE(expected_b.get() != NULL);
  scoped_ptr<BinaryValue> expected_c(
      BinaryValue::CreateWithCopiedBuffer(block2, block2_size));
  ASSERT_TRUE(expected_c.get() != NULL);

  ListValue expected_list;
  RawDataPresenter::AppendResultWithKey(
      &expected_list, keys::kRequestBodyRawBytesKey, expected_a.release());
  RawDataPresenter::AppendResultWithKey(
      &expected_list, keys::kRequestBodyRawFileKey, expected_b.release());
  RawDataPresenter::AppendResultWithKey(
      &expected_list, keys::kRequestBodyRawBytesKey, expected_c.release());

  // Real output.
  RawDataPresenter raw_presenter;
  raw_presenter.FeedNextBytes(block1, block1_size);
  raw_presenter.FeedNextFile(kFilename);
  raw_presenter.FeedNextBytes(block2, block2_size);
  EXPECT_TRUE(raw_presenter.Succeeded());
  scoped_ptr<Value> result = raw_presenter.Result();
  ASSERT_TRUE(result.get() != NULL);

  EXPECT_TRUE(result->Equals(&expected_list));
}

}  // namespace extensions
