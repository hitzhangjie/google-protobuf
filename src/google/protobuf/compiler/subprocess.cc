// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// https://developers.google.com/protocol-buffers/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Author: kenton@google.com (Kenton Varda)

#include <google/protobuf/compiler/subprocess.h>

#include <algorithm>
#include <iostream>

#ifndef _WIN32
#include <errno.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <signal.h>
#endif

#include <google/protobuf/stubs/logging.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/message.h>
#include <google/protobuf/stubs/substitute.h>


namespace google {
namespace protobuf {
namespace compiler {

#ifdef _WIN32

static void CloseHandleOrDie(HANDLE handle) {
  if (!CloseHandle(handle)) {
    GOOGLE_LOG(FATAL) << "CloseHandle: "
                      << Subprocess::Win32ErrorMessage(GetLastError());
  }
}

Subprocess::Subprocess()
    : process_start_error_(ERROR_SUCCESS),
      child_handle_(NULL), child_stdin_(NULL), child_stdout_(NULL) {}

Subprocess::~Subprocess() {
  if (child_stdin_ != NULL) {
    CloseHandleOrDie(child_stdin_);
  }
  if (child_stdout_ != NULL) {
    CloseHandleOrDie(child_stdout_);
  }
}

void Subprocess::Start(const string& program, SearchMode search_mode) {
  // Create the pipes.
  HANDLE stdin_pipe_read;
  HANDLE stdin_pipe_write;
  HANDLE stdout_pipe_read;
  HANDLE stdout_pipe_write;

  if (!CreatePipe(&stdin_pipe_read, &stdin_pipe_write, NULL, 0)) {
    GOOGLE_LOG(FATAL) << "CreatePipe: " << Win32ErrorMessage(GetLastError());
  }
  if (!CreatePipe(&stdout_pipe_read, &stdout_pipe_write, NULL, 0)) {
    GOOGLE_LOG(FATAL) << "CreatePipe: " << Win32ErrorMessage(GetLastError());
  }

  // Make child side of the pipes inheritable.
  if (!SetHandleInformation(stdin_pipe_read,
                            HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
    GOOGLE_LOG(FATAL) << "SetHandleInformation: "
                      << Win32ErrorMessage(GetLastError());
  }
  if (!SetHandleInformation(stdout_pipe_write,
                            HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
    GOOGLE_LOG(FATAL) << "SetHandleInformation: "
                      << Win32ErrorMessage(GetLastError());
  }

  // Setup STARTUPINFO to redirect handles.
  STARTUPINFOA startup_info;
  ZeroMemory(&startup_info, sizeof(startup_info));
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESTDHANDLES;
  startup_info.hStdInput = stdin_pipe_read;
  startup_info.hStdOutput = stdout_pipe_write;
  startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  if (startup_info.hStdError == INVALID_HANDLE_VALUE) {
    GOOGLE_LOG(FATAL) << "GetStdHandle: "
                      << Win32ErrorMessage(GetLastError());
  }

  // CreateProcess() mutates its second parameter.  WTF?
  char* name_copy = strdup(program.c_str());

  // Create the process.
  PROCESS_INFORMATION process_info;

  if (CreateProcessA((search_mode == SEARCH_PATH) ? NULL : program.c_str(),
                     (search_mode == SEARCH_PATH) ? name_copy : NULL,
                     NULL,  // process security attributes
                     NULL,  // thread security attributes
                     TRUE,  // inherit handles?
                     0,     // obscure creation flags
                     NULL,  // environment (inherit from parent)
                     NULL,  // current directory (inherit from parent)
                     &startup_info,
                     &process_info)) {
    child_handle_ = process_info.hProcess;
    CloseHandleOrDie(process_info.hThread);
    child_stdin_ = stdin_pipe_write;
    child_stdout_ = stdout_pipe_read;
  } else {
    process_start_error_ = GetLastError();
    CloseHandleOrDie(stdin_pipe_write);
    CloseHandleOrDie(stdout_pipe_read);
  }

  CloseHandleOrDie(stdin_pipe_read);
  CloseHandleOrDie(stdout_pipe_write);
  free(name_copy);
}

bool Subprocess::Communicate(const Message& input, Message* output,
                             string* error) {
  if (process_start_error_ != ERROR_SUCCESS) {
    *error = Win32ErrorMessage(process_start_error_);
    return false;
  }

  GOOGLE_CHECK(child_handle_ != NULL) << "Must call Start() first.";

  string input_data = input.SerializeAsString();
  string output_data;

  int input_pos = 0;

  while (child_stdout_ != NULL) {
    HANDLE handles[2];
    int handle_count = 0;

    if (child_stdin_ != NULL) {
      handles[handle_count++] = child_stdin_;
    }
    if (child_stdout_ != NULL) {
      handles[handle_count++] = child_stdout_;
    }

    DWORD wait_result =
        WaitForMultipleObjects(handle_count, handles, FALSE, INFINITE);

    HANDLE signaled_handle = NULL;
    if (wait_result >= WAIT_OBJECT_0 &&
        wait_result < WAIT_OBJECT_0 + handle_count) {
      signaled_handle = handles[wait_result - WAIT_OBJECT_0];
    } else if (wait_result == WAIT_FAILED) {
      GOOGLE_LOG(FATAL) << "WaitForMultipleObjects: "
                        << Win32ErrorMessage(GetLastError());
    } else {
      GOOGLE_LOG(FATAL) << "WaitForMultipleObjects: Unexpected return code: "
                        << wait_result;
    }

    if (signaled_handle == child_stdin_) {
      DWORD n;
      if (!WriteFile(child_stdin_,
                     input_data.data() + input_pos,
                     input_data.size() - input_pos,
                     &n, NULL)) {
        // Child closed pipe.  Presumably it will report an error later.
        // Pretend we're done for now.
        input_pos = input_data.size();
      } else {
        input_pos += n;
      }

      if (input_pos == input_data.size()) {
        // We're done writing.  Close.
        CloseHandleOrDie(child_stdin_);
        child_stdin_ = NULL;
      }
    } else if (signaled_handle == child_stdout_) {
      char buffer[4096];
      DWORD n;

      if (!ReadFile(child_stdout_, buffer, sizeof(buffer), &n, NULL)) {
        // We're done reading.  Close.
        CloseHandleOrDie(child_stdout_);
        child_stdout_ = NULL;
      } else {
        output_data.append(buffer, n);
      }
    }
  }

  if (child_stdin_ != NULL) {
    // Child did not finish reading input before it closed the output.
    // Presumably it exited with an error.
    CloseHandleOrDie(child_stdin_);
    child_stdin_ = NULL;
  }

  DWORD wait_result = WaitForSingleObject(child_handle_, INFINITE);

  if (wait_result == WAIT_FAILED) {
    GOOGLE_LOG(FATAL) << "WaitForSingleObject: "
                      << Win32ErrorMessage(GetLastError());
  } else if (wait_result != WAIT_OBJECT_0) {
    GOOGLE_LOG(FATAL) << "WaitForSingleObject: Unexpected return code: "
                      << wait_result;
  }

  DWORD exit_code;
  if (!GetExitCodeProcess(child_handle_, &exit_code)) {
    GOOGLE_LOG(FATAL) << "GetExitCodeProcess: "
                      << Win32ErrorMessage(GetLastError());
  }

  CloseHandleOrDie(child_handle_);
  child_handle_ = NULL;

  if (exit_code != 0) {
    *error = strings::Substitute(
        "Plugin failed with status code $0.", exit_code);
    return false;
  }

  if (!output->ParseFromString(output_data)) {
    *error = "Plugin output is unparseable: " + CEscape(output_data);
    return false;
  }

  return true;
}

string Subprocess::Win32ErrorMessage(DWORD error_code) {
  char* message;

  // WTF?
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 NULL, error_code, 0,
                 (LPSTR)&message,  // NOT A BUG!
                 0, NULL);

  string result = message;
  LocalFree(message);
  return result;
}

// ===================================================================

#else  // _WIN32

Subprocess::Subprocess()
    : child_pid_(-1), child_stdin_(-1), child_stdout_(-1) {}

Subprocess::~Subprocess() {
  if (child_stdin_ != -1) {
    close(child_stdin_);
  }
  if (child_stdout_ != -1) {
    close(child_stdout_);
  }
}

void Subprocess::Start(const string& program, SearchMode search_mode) {
  // Note that we assume that there are no other threads, thus we don't have to
  // do crazy stuff like using socket pairs or avoiding libc locks.

  // [0] is read end, [1] is write end.
  int stdin_pipe[2];
  int stdout_pipe[2];

  GOOGLE_CHECK(pipe(stdin_pipe) != -1);
  GOOGLE_CHECK(pipe(stdout_pipe) != -1);

  char* argv[2] = { strdup(program.c_str()), NULL };

  child_pid_ = fork();
  if (child_pid_ == -1) {
    GOOGLE_LOG(FATAL) << "fork: " << strerror(errno);
  } else if (child_pid_ == 0) {
    // We are the child.
    // 将子进程的stdin重定向到stdin_pipe的读端
    dup2(stdin_pipe[0], STDIN_FILENO);
    // 将子进程的stdout重定向到stdout_pipe的写端
    dup2(stdout_pipe[1], STDOUT_FILENO);

    // 子进程通过0、1对管道进行操作就够了，释放多余的fd
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);

    // 根据程序搜索模式调用exec族函数来调用插件执行，exec族函数通过替换当前进
    // 程的代码段、数据段等内存数据信息，然后调整寄存器信息，使得进程转而去执
    // 行插件的代码。插件代码执行之前进程就已经将fd 0、1重定向到父进程clone过
    // 来的管道了，因此插件程序的输出将直接被输出到父进程创建的管道中。
    // 正常情况下，exec一旦执行成功，那么久绝不对执行switch后续的代码了，只有
    // 出错才可能会执行到后续的代码。
    switch (search_mode) {
      case SEARCH_PATH:
        execvp(argv[0], argv);
        break;
      case EXACT_NAME:
        execv(argv[0], argv);
        break;
    }

    // 只有出错才可能会执行到这里的代码。
    //
    // Write directly to STDERR_FILENO to avoid stdio code paths that may do
    // stuff that is unsafe here.
    int ignored;
    ignored = write(STDERR_FILENO, argv[0], strlen(argv[0]));
    const char* message = ": program not found or is not executable\n";
    ignored = write(STDERR_FILENO, message, strlen(message));
    (void) ignored;

    // Must use _exit() rather than exit() to avoid flushing output buffers
    // that will also be flushed by the parent.
    _exit(1);
  } else {
    free(argv[0]);

    // 父进程释放无用的fd
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    // 子进程的stdin，对父进程来说也就是管道stdin_pipe的写端，CodeGeneratorRequest将通过这个fd写给子进程
    child_stdin_ = stdin_pipe[1];
    // 子进程的stdout，对父进程来说也就是管道stdout_pipe的读端，CodeGeneratorResponse将通过这个fd从子进程读取
    child_stdout_ = stdout_pipe[0];
  }
}

bool Subprocess::Communicate(const Message& input, Message* output,
                             string* error) {

  GOOGLE_CHECK_NE(child_stdin_, -1) << "Must call Start() first.";

  // The "sighandler_t" typedef is GNU-specific, so define our own.
  typedef void SignalHandler(int);

  // Make sure SIGPIPE is disabled so that if the child dies it doesn't kill us.
  SignalHandler* old_pipe_handler = signal(SIGPIPE, SIG_IGN);

  string input_data = input.SerializeAsString();
  string output_data;

  int input_pos = 0;
  int max_fd = std::max(child_stdin_, child_stdout_);

  // child_stdout==-1的时候表示子进程返回的数据已经读取完毕了，可以gg了
  while (child_stdout_ != -1) {
    fd_set read_fds;
    fd_set write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    if (child_stdout_ != -1) {
      FD_SET(child_stdout_, &read_fds);
    }
    if (child_stdin_ != -1) {
      FD_SET(child_stdin_, &write_fds);
    }

    // 这种情景下也用select，果然很google！
    if (select(max_fd + 1, &read_fds, &write_fds, NULL, NULL) < 0) {
      if (errno == EINTR) {
        // Interrupted by signal.  Try again.
        continue;
      } else {
        GOOGLE_LOG(FATAL) << "select: " << strerror(errno);
      }
    }
    
    // stdout_pipe写事件就绪，写请求CodeGeneratorRequest给子进程
    if (child_stdin_ != -1 && FD_ISSET(child_stdin_, &write_fds)) {
      int n = write(child_stdin_, input_data.data() + input_pos,
                                  input_data.size() - input_pos);
      if (n < 0) {
        // Child closed pipe.  Presumably it will report an error later.
        // Pretend we're done for now.
        input_pos = input_data.size();
      } else {
        input_pos += n;
      }

      // 代码生成请求已经成功写给子进程了，关闭相关的fd
      if (input_pos == input_data.size()) {
        // We're done writing.  Close.
        close(child_stdin_);
        child_stdin_ = -1;
      }
    }

    // stdin_pipe读事件就绪，读取子进程返回的CodeGeneratorResponse
    if (child_stdout_ != -1 && FD_ISSET(child_stdout_, &read_fds)) {
      char buffer[4096];
      int n = read(child_stdout_, buffer, sizeof(buffer));

      if (n > 0) {
        output_data.append(buffer, n);
      } else {
        // 子进程返回的CodeGeneratorResponse已经读取完毕，关闭相关的fd
        close(child_stdout_);
        child_stdout_ = -1;
      }
    }
  }

  // 子进程还没有读取CodeGeneratorRequest完毕，就关闭了输出，这种情况下也不可
  // 能读取到返回的CodeGeneratorResponse了，这种情况很可能是出现了异常。
  if (child_stdin_ != -1) {
    // Child did not finish reading input before it closed the output.
    // Presumably it exited with an error.
    close(child_stdin_);
    child_stdin_ = -1;
  }

  // 等待子进程结束，子进程退出之后，需要父进程来清理子进程占用的部分资源。
  // 如果当前父进程不waitpid的话，子进程的父进程会变为init或者systemd进程，同样也会被清理的。
  int status;
  while (waitpid(child_pid_, &status, 0) == -1) {
    if (errno != EINTR) {
      GOOGLE_LOG(FATAL) << "waitpid: " << strerror(errno);
    }
  }

  // 刚才为了阻止SIGPIPE信号到达时导致进程终止，我们修改了SIGPIPE的信号处理函
  // 数，这里可以恢复之前的SIGPIPE的信号处理函数。
  signal(SIGPIPE, old_pipe_handler);

  // 根据子进程的退出状态执行后续的处理逻辑
  // - 异常处理
  if (WIFEXITED(status)) {
    if (WEXITSTATUS(status) != 0) {
      int error_code = WEXITSTATUS(status);
      *error = strings::Substitute(
          "Plugin failed with status code $0.", error_code);
      return false;
    }
  } else if (WIFSIGNALED(status)) {
    int signal = WTERMSIG(status);
    *error = strings::Substitute(
        "Plugin killed by signal $0.", signal);
    return false;
  } else {
    *error = "Neither WEXITSTATUS nor WTERMSIG is true?";
    return false;
  }

  // 将子进程返回的串行化之后的CodeGeneratorResponse数据进行反串行化，反串行化
  // 成Message对象，实际上这里的Message::ParseFromString(const string&)是个虚
  // 函数，是被CodeGeneratorResponse这个类重写了的，反串行化过程与具体的类密切
  // 相关，也必须在派生类中予以实现。
  if (!output->ParseFromString(output_data)) {
    *error = "Plugin output is unparseable: " + CEscape(output_data);
    return false;
  }

  return true;
}

#endif  // !_WIN32

}  // namespace compiler
}  // namespace protobuf
}  // namespace google
