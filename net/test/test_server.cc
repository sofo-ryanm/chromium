// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/test/test_server.h"

#include <algorithm>
#include <string>
#include <vector>

#include "build/build_config.h"

#if defined(OS_WIN)
#include <windows.h>
#include <wincrypt.h>
#elif defined(OS_MACOSX)
#include "net/base/x509_certificate.h"
#endif

#include "base/file_util.h"
#include "base/leak_annotations.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "net/base/cert_test_util.h"
#include "net/base/host_resolver.h"
#include "net/base/test_completion_callback.h"
#include "net/socket/tcp_client_socket.h"
#include "net/socket/tcp_pinger.h"
#include "testing/platform_test.h"

#if defined(OS_WIN)
#pragma comment(lib, "crypt32.lib")
#endif

namespace {

// Number of connection attempts for tests.
const int kServerConnectionAttempts = 10;

// Connection timeout in milliseconds for tests.
const int kServerConnectionTimeoutMs = 1000;

}  // namespace

namespace net {

#if defined(OS_MACOSX)
void SetMacTestCertificate(X509Certificate* cert);
#endif

// static
const char TestServerLauncher::kHostName[] = "127.0.0.1";
const char TestServerLauncher::kMismatchedHostName[] = "localhost";
const int TestServerLauncher::kOKHTTPSPort = 9443;
const int TestServerLauncher::kBadHTTPSPort = 9666;

// The issuer name of the cert that should be trusted for the test to work.
const wchar_t TestServerLauncher::kCertIssuerName[] = L"Test CA";

TestServerLauncher::TestServerLauncher()
    : process_handle_(base::kNullProcessHandle),
      ssl_client_auth_(false) {
  InitCertPath();
}

void TestServerLauncher::InitCertPath() {
  PathService::Get(base::DIR_SOURCE_ROOT, &cert_dir_);
  cert_dir_ = cert_dir_.Append(FILE_PATH_LITERAL("net"))
                       .Append(FILE_PATH_LITERAL("data"))
                       .Append(FILE_PATH_LITERAL("ssl"))
                       .Append(FILE_PATH_LITERAL("certificates"));
}

namespace {

void AppendToPythonPath(const FilePath& dir) {
  // Do nothing if dir already on path.

#if defined(OS_WIN)
  const wchar_t kPythonPath[] = L"PYTHONPATH";
  // TODO(dkegel): handle longer PYTHONPATH variables
  wchar_t oldpath[4096];
  if (GetEnvironmentVariable(kPythonPath, oldpath, arraysize(oldpath)) == 0) {
    SetEnvironmentVariableW(kPythonPath, dir.value().c_str());
  } else if (!wcsstr(oldpath, dir.value().c_str())) {
    std::wstring newpath(oldpath);
    newpath.append(L";");
    newpath.append(dir.value());
    SetEnvironmentVariableW(kPythonPath, newpath.c_str());
  }
#elif defined(OS_POSIX)
  const char kPythonPath[] = "PYTHONPATH";
  const char* oldpath = getenv(kPythonPath);
  // setenv() leaks memory intentionally on Mac
  if (!oldpath) {
    setenv(kPythonPath, dir.value().c_str(), 1);
  } else if (!strstr(oldpath, dir.value().c_str())) {
    std::string newpath(oldpath);
    newpath.append(":");
    newpath.append(dir.value());
    setenv(kPythonPath, newpath.c_str(), 1);
  }
#endif
}

}  // end namespace

void TestServerLauncher::SetPythonPath() {
  FilePath third_party_dir;
  CHECK(PathService::Get(base::DIR_SOURCE_ROOT, &third_party_dir));
  third_party_dir = third_party_dir.Append(FILE_PATH_LITERAL("third_party"));

  AppendToPythonPath(third_party_dir.Append(FILE_PATH_LITERAL("tlslite")));
  AppendToPythonPath(third_party_dir.Append(FILE_PATH_LITERAL("pyftpdlib")));

  // Locate the Python code generated by the protocol buffers compiler.
  FilePath generated_code_dir;
  CHECK(PathService::Get(base::DIR_EXE, &generated_code_dir));
  generated_code_dir = generated_code_dir.Append(FILE_PATH_LITERAL("pyproto"));
  AppendToPythonPath(generated_code_dir);
  AppendToPythonPath(generated_code_dir.Append(FILE_PATH_LITERAL("sync_pb")));
}

bool TestServerLauncher::Start(Protocol protocol,
                               const std::string& host_name, int port,
                               const FilePath& document_root,
                               const FilePath& cert_path,
                               const std::wstring& file_root_url) {
  if (!cert_path.value().empty()) {
    if (!LoadTestRootCert())
      return false;
    if (!CheckCATrusted())
      return false;
  }

  std::string port_str = base::IntToString(port);

  // Get path to python server script
  FilePath testserver_path;
  if (!PathService::Get(base::DIR_SOURCE_ROOT, &testserver_path))
    return false;
  testserver_path = testserver_path
      .Append(FILE_PATH_LITERAL("net"))
      .Append(FILE_PATH_LITERAL("tools"))
      .Append(FILE_PATH_LITERAL("testserver"))
      .Append(FILE_PATH_LITERAL("testserver.py"));

  PathService::Get(base::DIR_SOURCE_ROOT, &document_root_dir_);
  document_root_dir_ = document_root_dir_.Append(document_root);

  SetPythonPath();

#if defined(OS_WIN)
  // Get path to python interpreter
  FilePath python_exe;
  if (!PathService::Get(base::DIR_SOURCE_ROOT, &python_exe))
    return false;
  python_exe = python_exe
      .Append(FILE_PATH_LITERAL("third_party"))
      .Append(FILE_PATH_LITERAL("python_24"))
      .Append(FILE_PATH_LITERAL("python.exe"));

  std::wstring command_line =
      L"\"" + python_exe.ToWStringHack() + L"\" " +
      L"\"" + testserver_path.ToWStringHack() +
      L"\" --port=" + UTF8ToWide(port_str) +
      L" --data-dir=\"" + document_root_dir_.ToWStringHack() + L"\"";
  if (protocol == ProtoFTP)
    command_line.append(L" -f");
  if (!cert_path.value().empty()) {
    command_line.append(L" --https=\"");
    command_line.append(cert_path.ToWStringHack());
    command_line.append(L"\"");
  }
  if (!file_root_url.empty()) {
    command_line.append(L" --file-root-url=\"");
    command_line.append(file_root_url);
    command_line.append(L"\"");
  }
  if (ssl_client_auth_)
    command_line.append(L" --ssl-client-auth");

  if (!LaunchTestServerAsJob(command_line,
                             true,
                             &process_handle_,
                             &job_handle_)) {
    LOG(ERROR) << "Failed to launch " << command_line;
    return false;
  }
#elif defined(OS_POSIX)
  std::vector<std::string> command_line;
  command_line.push_back("python");
  command_line.push_back(testserver_path.value());
  command_line.push_back("--port=" + port_str);
  command_line.push_back("--data-dir=" + document_root_dir_.value());
  if (protocol == ProtoFTP)
    command_line.push_back("-f");
  if (!cert_path.value().empty())
    command_line.push_back("--https=" + cert_path.value());
  if (ssl_client_auth_)
    command_line.push_back("--ssl-client-auth");

  base::file_handle_mapping_vector no_mappings;
  LOG(INFO) << "Trying to launch " << command_line[0] << " ...";
  if (!base::LaunchApp(command_line, no_mappings, false, &process_handle_)) {
    LOG(ERROR) << "Failed to launch " << command_line[0] << " ...";
    return false;
  }
#endif

  // Let the server start, then verify that it's up.
  // Our server is Python, and takes about 500ms to start
  // up the first time, and about 200ms after that.
  if (!WaitToStart(host_name, port)) {
    LOG(ERROR) << "Failed to connect to server";
    Stop();
    return false;
  }

  LOG(INFO) << "Started on port " << port_str;
  return true;
}

bool TestServerLauncher::WaitToStart(const std::string& host_name, int port) {
  // Verify that the webserver is actually started.
  // Otherwise tests can fail if they run faster than Python can start.
  net::AddressList addr;
  scoped_refptr<net::HostResolver> resolver(
      net::CreateSystemHostResolver(net::HostResolver::kDefaultParallelism));
  net::HostResolver::RequestInfo info(host_name, port);
  int rv = resolver->Resolve(info, &addr, NULL, NULL, BoundNetLog());
  if (rv != net::OK)
    return false;

  net::TCPPinger pinger(addr);
  rv = pinger.Ping(
      base::TimeDelta::FromMilliseconds(kServerConnectionTimeoutMs),
      kServerConnectionAttempts);
  return rv == net::OK;
}

bool TestServerLauncher::WaitToFinish(int timeout_ms) {
  if (!process_handle_)
    return true;

  bool ret = base::WaitForSingleProcess(process_handle_, timeout_ms);
  if (ret) {
    base::CloseProcessHandle(process_handle_);
    process_handle_ = base::kNullProcessHandle;
    LOG(INFO) << "Finished.";
  } else {
    LOG(INFO) << "Timed out.";
  }
  return ret;
}

bool TestServerLauncher::Stop() {
  if (!process_handle_)
    return true;

  // First check if the process has already terminated.
  bool ret = base::WaitForSingleProcess(process_handle_, 0);
  if (!ret)
    ret = base::KillProcess(process_handle_, 1, true);

  if (ret) {
    base::CloseProcessHandle(process_handle_);
    process_handle_ = base::kNullProcessHandle;
    LOG(INFO) << "Stopped.";
  } else {
    LOG(INFO) << "Kill failed?";
  }

  return ret;
}

TestServerLauncher::~TestServerLauncher() {
#if defined(OS_MACOSX)
  SetMacTestCertificate(NULL);
#endif
  Stop();
}

FilePath TestServerLauncher::GetRootCertPath() {
  FilePath path(cert_dir_);
  path = path.AppendASCII("root_ca_cert.crt");
  return path;
}

FilePath TestServerLauncher::GetOKCertPath() {
  FilePath path(cert_dir_);
  path = path.AppendASCII("ok_cert.pem");
  return path;
}

FilePath TestServerLauncher::GetExpiredCertPath() {
  FilePath path(cert_dir_);
  path = path.AppendASCII("expired_cert.pem");
  return path;
}

bool TestServerLauncher::LoadTestRootCert() {
#if defined(USE_NSS)
  if (cert_)
    return true;

  // TODO(dkegel): figure out how to get this to only happen once?

  // This currently leaks a little memory.
  // TODO(dkegel): fix the leak and remove the entry in
  // tools/valgrind/memcheck/suppressions.txt
  ANNOTATE_SCOPED_MEMORY_LEAK;  // Tell heap checker about the leak.
  cert_ = LoadTemporaryRootCert(GetRootCertPath());
  DCHECK(cert_);
  return (cert_ != NULL);
#elif defined(OS_MACOSX)
  X509Certificate* cert = LoadTemporaryRootCert(GetRootCertPath());
  if (!cert)
    return false;
  SetMacTestCertificate(cert);
  return true;
#else
  return true;
#endif
}

bool TestServerLauncher::CheckCATrusted() {
#if defined(OS_WIN)
  HCERTSTORE cert_store = CertOpenSystemStore(NULL, L"ROOT");
  if (!cert_store) {
    LOG(ERROR) << " could not open trusted root CA store";
    return false;
  }
  PCCERT_CONTEXT cert =
      CertFindCertificateInStore(cert_store,
                                 X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                 0,
                                 CERT_FIND_ISSUER_STR,
                                 kCertIssuerName,
                                 NULL);
  if (cert)
    CertFreeCertificateContext(cert);
  CertCloseStore(cert_store, 0);

  if (!cert) {
    LOG(ERROR) << " TEST CONFIGURATION ERROR: you need to import the test ca "
                  "certificate to your trusted roots for this test to work. "
                  "For more info visit:\n"
                  "http://dev.chromium.org/developers/testing\n";
    return false;
  }
#endif
  return true;
}

#if defined(OS_WIN)
bool LaunchTestServerAsJob(const std::wstring& cmdline,
                           bool start_hidden,
                           base::ProcessHandle* process_handle,
                           ScopedHandle* job_handle) {
  // Launch test server process.
  STARTUPINFO startup_info = {0};
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESHOWWINDOW;
  startup_info.wShowWindow = start_hidden ? SW_HIDE : SW_SHOW;
  PROCESS_INFORMATION process_info;

  // If this code is run under a debugger, the test server process is
  // automatically associated with a job object created by the debugger.
  // The CREATE_BREAKAWAY_FROM_JOB flag is used to prevent this.
  if (!CreateProcess(NULL,
                     const_cast<wchar_t*>(cmdline.c_str()), NULL, NULL,
                     FALSE, CREATE_BREAKAWAY_FROM_JOB, NULL, NULL,
                     &startup_info, &process_info)) {
    LOG(ERROR) << "Could not create process.";
    return false;
  }
  CloseHandle(process_info.hThread);

  // If the caller wants the process handle, we won't close it.
  if (process_handle) {
    *process_handle = process_info.hProcess;
  } else {
    CloseHandle(process_info.hProcess);
  }

  // Create a JobObject and associate the test server process with it.
  job_handle->Set(CreateJobObject(NULL, NULL));
  if (!job_handle->IsValid()) {
    LOG(ERROR) << "Could not create JobObject.";
    return false;
  } else {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info = {0};
    limit_info.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (0 == SetInformationJobObject(job_handle->Get(),
      JobObjectExtendedLimitInformation, &limit_info, sizeof(limit_info))) {
      LOG(ERROR) << "Could not SetInformationJobObject.";
      return false;
    }
    if (0 == AssignProcessToJobObject(job_handle->Get(),
                                      process_info.hProcess)) {
      LOG(ERROR) << "Could not AssignProcessToObject.";
      return false;
    }
  }
  return true;
}
#endif

}  // namespace net
