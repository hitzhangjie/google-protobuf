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

// 这个头文件定义了protoc的命令行接口
#include <google/protobuf/compiler/command_line_interface.h>
// protoc中内置了对cpp、python、java语言的支持，对其他语言的支持需要以plugin的方式来支持
#include <google/protobuf/compiler/cpp/cpp_generator.h>
#ifndef OPENSOURCE_PROTOBUF_CPP_BOOTSTRAP
#include <google/protobuf/compiler/python/python_generator.h>
#include <google/protobuf/compiler/java/java_generator.h>
#endif  // ! OPENSOURCE_PROTOBUF_CPP_BOOTSTRAP

#ifndef OPENSOURCE_PROTOBUF_CPP_BOOTSTRAP
#include <google/protobuf/compiler/csharp/csharp_generator.h>
#include <google/protobuf/compiler/javanano/javanano_generator.h>
#include <google/protobuf/compiler/js/js_generator.h>
#include <google/protobuf/compiler/objectivec/objectivec_generator.h>
#include <google/protobuf/compiler/php/php_generator.h>
#include <google/protobuf/compiler/ruby/ruby_generator.h>
#endif  // ! OPENSOURCE_PROTOBUF_CPP_BOOTSTRAP

int main(int argc, char* argv[]) {

  // 初始化protoc命令行接口并开启插件
  // - 插件只是普通的可执行程序，其文件名以AllowPlugins参数protoc-开头
  // - 假定protoc --foo_out，那么实际调用的插件是protoc-foo
  google::protobuf::compiler::CommandLineInterface cli;
  cli.AllowPlugins("protoc-");

  // Proto2 C++ (指定了--cpp_out将调用cpp::Generator)
  google::protobuf::compiler::cpp::CppGenerator cpp_generator;
  cli.RegisterGenerator("--cpp_out", "--cpp_opt", &cpp_generator,
                        "Generate C++ header and source.");

#ifndef OPENSOURCE_PROTOBUF_CPP_BOOTSTRAP
  // Proto2 Java (指定了--java_out将调用java::Generator)
  google::protobuf::compiler::java::JavaGenerator java_generator;
  cli.RegisterGenerator("--java_out", "--java_opt", &java_generator,
                        "Generate Java source file.");
#endif  // !OPENSOURCE_PROTOBUF_CPP_BOOTSTRAP


#ifndef OPENSOURCE_PROTOBUF_CPP_BOOTSTRAP
  // Proto2 Python (指定了python_out将调用python::Generator)
  google::protobuf::compiler::python::Generator py_generator;
  cli.RegisterGenerator("--python_out", &py_generator,
                        "Generate Python source file.");

  // Java Nano
  google::protobuf::compiler::javanano::JavaNanoGenerator javanano_generator;
  cli.RegisterGenerator("--javanano_out", &javanano_generator,
                        "Generate Java Nano source file.");

  // PHP ...
  google::protobuf::compiler::php::Generator php_generator;
  cli.RegisterGenerator("--php_out", &php_generator,
                        "Generate PHP source file.");

  // Ruby ...
  google::protobuf::compiler::ruby::Generator rb_generator;
  cli.RegisterGenerator("--ruby_out", &rb_generator,
                        "Generate Ruby source file.");

  // CSharp ...
  google::protobuf::compiler::csharp::Generator csharp_generator;
  cli.RegisterGenerator("--csharp_out", "--csharp_opt", &csharp_generator,
                        "Generate C# source file.");

  // Objective C ...
  google::protobuf::compiler::objectivec::ObjectiveCGenerator objc_generator;
  cli.RegisterGenerator("--objc_out", "--objc_opt", &objc_generator,
                        "Generate Objective C header and source.");

  // JavaScript ...
  google::protobuf::compiler::js::Generator js_generator;
  cli.RegisterGenerator("--js_out", &js_generator,
                        "Generate JavaScript source.");
#endif  // !OPENSOURCE_PROTOBUF_CPP_BOOTSTRAP

  // 解析proto、生成源代码(借助内置的generator或者plugins)、创建源代码文件
  return cli.Run(argc, argv);
}
