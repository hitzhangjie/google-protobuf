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
//  Based on original Protocol Buffers design by
//  Sanjay Ghemawat, Jeff Dean, and others.
//
// Implements the Protocol Compiler front-end such that it may be reused by
// custom compilers written to support other languages.

#ifndef GOOGLE_PROTOBUF_COMPILER_COMMAND_LINE_INTERFACE_H__
#define GOOGLE_PROTOBUF_COMPILER_COMMAND_LINE_INTERFACE_H__

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/stubs/hash.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <utility>

namespace google {
namespace protobuf {

//proto文件中定义的数据类型可通过FileDescriptor来遍历、查看
class Descriptor;            // descriptor.h
class DescriptorPool;        // descriptor.h
class FileDescriptor;        // descriptor.h
class FileDescriptorProto;   // descriptor.pb.h
template<typename T> class RepeatedPtrField;  // repeated_field.h

}  // namespace protobuf

namespace protobuf {
namespace compiler {

class CodeGenerator;        // code_generator.h
class GeneratorContext;      // code_generator.h
class DiskSourceTree;       // importer.h

// 这个类实现了protoc的命令行接口，使得protoc很容易扩展。
// 例如我们想让protoc既支持cpp又支持另一种语言foo，那么我们可以定义一个实现了
// CodeGenerator接口的FooGenerator，然后在protoc的main方法中对这两种语言cpp、Foo
// 及其对应的CodeGenerator进行注册。
//
//   int main(int argc, char* argv[]) {
//     google::protobuf::compiler::CommandLineInterface cli;
//
//     // 支持cpp
//     google::protobuf::compiler::cpp::CppGenerator cpp_generator;
//     cli.RegisterGenerator("--cpp_out", &cpp_generator, "Generate C++ source and header.");
//
//     // 支持foo
//     FooGenerator foo_generator;
//     cli.RegisterGenerator("--foo_out", &foo_generator, "Generate Foo file.");
//
//     return cli.Run(argc, argv);
//   }
//
// The compiler is invoked with syntax like:
//   protoc --cpp_out=outdir --foo_out=outdir --proto_path=src src/foo.proto
//
// For a full description of the command-line syntax, invoke it with --help.
class LIBPROTOC_EXPORT CommandLineInterface {
 public:
  CommandLineInterface();
  ~CommandLineInterface();

  // 为某种编程语言注册一个对应的代码生成器（其实这里也不一定非得是语言）
  //
  // 命令行接口的参数:
  // @param flag_name 指定输出文件类型的命令，例如--cpp_out，参数名字必须以“-”开头，
                      如果名字大于两个字符，则必须以“--”开头。
  // @param generator 与flag_name对应的CodeGenerator接口实现
  // @param help_text 执行protoc --help的时候对这里的flag_name的说明性信息
  //
  // 某些代码生成器可接受额外参数，这些参数在输出路径之前给出，与输出路径之间用“:”分隔。
  //   protoc --foo_out=enable_bar:outdir
  // 这里的:outdir之前的enable_bar被作为参数传递给CodeGenerator::Generate()的参数。
  void RegisterGenerator(const string& flag_name,
                         CodeGenerator* generator,
                         const string& help_text);

  // 为某种编程语言注册一个对应的代码生成器
  // ...
  // @param option_flag_name 指定额外的选项
  // ...
  //
  // 与前面一个函数RegisterGenerator所不同的是，这个重载函数多个参数
  // option_flag_name，通过这个函数注册的语言和代码生成器可以接受额外的参数。例
  // 如通过command_line_interface.RegisterGenerator("--foo_out", "--foo_opt", ...)
  // 注册了foo以及对应代码生成器，那么我们可以在执行protoc 的时候指定额外的参数
  // --foo_opt：protoc --foo_out=enable_bar:outdir --foo_opt=enable_baz，此时传
  // 递给代码生成器的参数将会包括enable_bar和enable_baz。
  void RegisterGenerator(const string& flag_name,
                         const string& option_flag_name,
                         CodeGenerator* generator,
                         const string& help_text);

  // RegisterGenerator方法是在protoc的main方法中进行语言、代码生成器的注册，在
  // 生产环境中不可能允许开发人员肆意修改公用程序库，这意味着我们如果要在稳定地
  // protoc v2.5.0基础上进行源码的修改这条路是行不通的，那么如何自由地扩展其功
  // 能呢？protoc提供了“plugin”机制，我们可以通过自定义插件来实现对其他语言
  //（甚至不是语言）的支持。
  // 开启protoc对插件的支持，这种模式下，如果一个命令行选项以_out结尾，例如
  // --xxx_out，但是在protoc已经注册的语言支持中没有找到匹配的语言及代码生成器，
  // 这个时候protoc就会去检查是否有匹配的插件支持这种语言，将这个插件来作为代
  // 码生成器使用。这里的的protoc插件是一个$PATH中可搜索到的可执行程序，当然
  // 这个可执行程序稍微有点特殊。
  // 这里插件名称（可执行程序名称）是如何确定的呢？选项--xxx_out中，截取“xxx”，
  // 然后根据${exe_name_prefix}以及xxx来拼接出一个插件的名字，假如${exe_name_prefix}
  // 是protoc-，那么插件的名字就是protoc-xxx，protoc将尝试执行这个程序来完成代
  // 码生成的工作。
  // 假定插件的名字是plugin，protoc是这样调用这个插件的：
  //   plugin [--out=OUTDIR] [--parameter=PARAMETER] PROTO_FILES < DESCRIPTORS
  // 选项说明：
  // --out：指明了插件代码生成时的输出目录（跟通过--foo_out传递给protoc的一样）,
  //        如果省略这个参数，输出目录就是当前目录。
  // --parameter：指明了传递给代码生成器的参数。
  // PROTO_FILES：指明了protoc调用时传递给protoc的待处理的.proto文件列表。
  // DESCRIPTORS: 编码后的FileDescriptorSet（这个在descriptor.proto中定义），
  // 这里编码后的数据通过管道重定向到插件的标准输入，这里的FileDescriptorSet包括
  // PROTO_FILES中列出的所有proto文件的descriptors，也包括这些PROTO_FILES中
  // proto文件import进来的其他proto文件。插件不应该直接读取PROTO_FILES中的
  // proto文件，而应该使用这里的DESCRIPTORS。
  //
  // 插件跟protoc main函数中注册的代码生成器一样，它也需要生成所有必须的文件。
  // 插件会将所有要生成的文件的名字写到stdout，插件名字是相对于当前输出目录的。如
  // 果插件工作过程中发生了错误，需要将错误信息写到stderr，如果发生了严重错误，
  // 插件应该退出并返回一个非0的返回码。插件写出的数据会被protoc读取并执行后续
  // 处理逻辑。
  void AllowPlugins(const string& exe_name_prefix);

  // 根据指定的命令行参数来执行protocol compiler，返回值将由main返回。
  //
  // Run()方法是非线程安全的，因为其中调用了strerror()，不要在多线程环境下使用。
  int Run(int argc, const char* const argv[]);

  // proto路径解析的控制说明，fixme
  // Call SetInputsAreCwdRelative(true) if the input files given on the command
  // line should be interpreted relative to the proto import path specified
  // using --proto_path or -I flags.  Otherwise, input file names will be
  // interpreted relative to the current working directory (or as absolute
  // paths if they start with '/'), though they must still reside inside
  // a directory given by --proto_path or the compiler will fail.  The latter
  // mode is generally more intuitive and easier to use, especially e.g. when
  // defining implicit rules in Makefiles.
  void SetInputsAreProtoPathRelative(bool enable) {
    inputs_are_proto_path_relative_ = enable;
  }

  // 设置执行protoc --version时打印的版本相关的信息，这行版本信息的下一行也会打印libprotoc的版本。
  void SetVersionInfo(const string& text) {
    version_info_ = text;
  }


 private:
  // -----------------------------------------------------------------
 
  // 这个类的后续部分代码，虽然也比较重要，但是即便在这里先不解释，也不会给我
  // 们的理解造成太多干扰，为了简化篇幅并且避免过早地陷入细节而偏离对整体的把
  // 握，这里我先把这个类的后续部分代码进行删减……只保留相对比较重要的。

  class ErrorPrinter;
  class GeneratorContextImpl;
  class MemoryOutputStream;
  typedef hash_map<string, GeneratorContextImpl*> GeneratorContextMap;

  // 清楚上次Run()运行时设置的状态
  void Clear();

  // 映射input_files_中的每个文件，使其变成相对于proto_path_中对应目录的相对路径
  // - 当inputs_are_proto_path_relative_为false的时候才会调用这个函数；
  // - 出错返回false，反之返回true；
  bool MakeInputsBeProtoPathRelative(DiskSourceTree* source_tree);

  // ParseArguments() & InterpretArgument()返回的状态
  enum ParseArgumentStatus {
    PARSE_ARGUMENT_DONE_AND_CONTINUE,
    PARSE_ARGUMENT_DONE_AND_EXIT,
    PARSE_ARGUMENT_FAIL
  };

  // 解析所有的命令行参数
  ParseArgumentStatus ParseArguments(int argc, const char* const argv[]);

  // 解析某个命令行参数
  // - 参数名放name，参数值放value
  // - 如果argv中的下一个参数应该被当做value则返回true，反之返回false
  bool ParseArgument(const char* arg, string* name, string* value);

  // 解析某个命令行参数的状态
  ParseArgumentStatus InterpretArgument(const string& name,
                                        const string& value);
  // Print the --help text to stderr.
  void PrintHelpText();

  // 描述了一个输出目录下如何从输入的proto文件生成指定的源代码文件
  struct OutputDirective;

  // 对解析成功的每个proto文件，生成对应的源代码文件
  // @param parsed_files 解析成功的proto文件vector
  // @param output_directive 输出指示，包括了文件名、语言、代码生成器、输出目录
  // @param generator_context 代码生成器上下文，可记录待输出文件名、文件内容、尺寸等信息
  bool GenerateOutput(const std::vector<const FileDescriptor*>& parsed_files,
                      const OutputDirective& output_directive,
                      GeneratorContext* generator_context);

  // 对解析成功的每个proto文件，调用protoc插件生成对应的源代码
  // @param parsed_files 解析成功的proto文件vector
  // @param plugin_name 插件的名称，命名方式一般是protoc-gen-${lang}
  // @param parameter 传递给protoc插件的参数
  // @param generator_context 代码生成器上下文，可记录待输出文件名、文件内容、尺寸等信息
  // @param error 错误信息
  bool GeneratePluginOutput(
      const std::vector<const FileDescriptor*>& parsed_files,
      const string& plugin_name, const string& parameter,
      GeneratorContext* generator_context, string* error);

  // 编码、解码，实现命令行中的--encode和--decode选项
  bool EncodeOrDecode(const DescriptorPool* pool);

  // 实现命令行中的--descriptor_set_out选项
  bool WriteDescriptorSet(const std::vector<const FileDescriptor*>& parsed_files);

  // Implements the --dependency_out option
  bool GenerateDependencyManifestFile(
      const std::vector<const FileDescriptor*>& parsed_files,
      const GeneratorContextMap& output_directories,
      DiskSourceTree* source_tree);

  // 获取指定proto文件依赖的proto文件列表（列表中包括该proto文件本身）
  // - proto文件通过FileDescriptorProto表示；
  // - 这些依赖的proto文件列表会被重新排序，被依赖的proto会被排在依它的proto前面,
  //   这样我们就可以调用DescriptorPool::BuildFile()来建立最终的源代码文件；
  // - already_seen中已经列出的proto文件不会被重复添加，每一个被添加的proto文件都被加入到already_seen中；
  // - 如果include_source_code_info为true，则包括源代码信息到FileDescriptorProtos中；
  static void GetTransitiveDependencies(
      const FileDescriptor* file,
      bool include_json_name,
      bool include_source_code_info,
      std::set<const FileDescriptor*>* already_seen,
      RepeatedPtrField<FileDescriptorProto>* output);

  // Implements the --print_free_field_numbers. This function prints free field
  // numbers into stdout for the message and it's nested message types in
  // post-order, i.e. nested types first. Printed range are left-right
  // inclusive, i.e. [a, b].
  //
  // Groups:
  // For historical reasons, groups are considered to share the same
  // field number space with the parent message, thus it will not print free
  // field numbers for groups. The field numbers used in the groups are
  // excluded in the free field numbers of the parent message.
  //
  // Extension Ranges:
  // Extension ranges are considered ocuppied field numbers and they will not be
  // listed as free numbers in the output.
  void PrintFreeFieldNumbers(const Descriptor* descriptor);

  // -----------------------------------------------------------------

  // 当前被调用的程序的名称，argv[0]
  string executable_name_;

  // 通过SetVersionInfo()设置的版本信息
  string version_info_;

  // 注册的代码生成器
  struct GeneratorInfo {
    string flag_name;
    string option_flag_name;
    CodeGenerator* generator;
    string help_text;
  };
  typedef std::map<string, GeneratorInfo> GeneratorMap;
  // flag_name、代码生成器map
  GeneratorMap generators_by_flag_name_;
  // option_name、代码生成器map
  GeneratorMap generators_by_option_name_;

  // flag_name、option map
  // - 如果调用protoc --foo_out=outputdir --foo_opt=enable_bar ...，
  //   map中将包括一个<--foo_out,enable_bar> entry.
  std::map<string, string> generator_parameters_;

  // Similar to generator_parameters_, but stores the parameters for plugins.
  std::map<string, string> plugin_parameters_;

  // protoc插件前缀，如果该变量为空，那么不允许使用插件
  // @see AllowPlugins()
  string plugin_prefix_;

  // 将protoc插件名称映射为具体的插件可执行文件
  // - 执行一个插件可执行程序时，首先搜索这个map，如果找到则直接执行；
  // - 如果这个map中找不到匹配的插件可执行程序，则搜索PATH寻找可执行程序执行；
  std::map<string, string> plugins_;

  // protoc命令行中指定的工作模式
  enum Mode {
    MODE_COMPILE,  // Normal mode:  parse .proto files and compile them.
    MODE_ENCODE,   // --encode:  read text from stdin, write binary to stdout.
    MODE_DECODE,   // --decode:  read binary from stdin, write text to stdout.
    MODE_PRINT,    // Print mode: print info of the given .proto files and exit.
  };

  Mode mode_;

  enum PrintMode {
    PRINT_NONE,               // Not in MODE_PRINT
    PRINT_FREE_FIELDS,        // --print_free_fields
  };

  PrintMode print_mode_;

  enum ErrorFormat {
    ERROR_FORMAT_GCC,   // GCC error output format (default).
    ERROR_FORMAT_MSVS   // Visual Studio output (--error_format=msvs).
  };

  ErrorFormat error_format_;

  std::vector<std::pair<string, string> >
      proto_path_;                   // Search path for proto files.
  std::vector<string> input_files_;  // Names of the input proto files.

  // Names of proto files which are allowed to be imported. Used by build
  // systems to enforce depend-on-what-you-import.
  std::set<string> direct_dependencies_;
  bool direct_dependencies_explicitly_set_;

  // If there's a violation of depend-on-what-you-import, this string will be
  // presented to the user. "%s" will be replaced with the violating import.
  string direct_dependencies_violation_msg_;

  // protoc调用时每个--${lang}_out都对应着一个OutputDirective
  struct OutputDirective {
    string name;                // E.g. "--foo_out"
    CodeGenerator* generator;   // NULL for plugins
    string parameter;
    string output_location;
  };
  // 一次protoc调用可能会同时制定多个--${lang}_out选项
  std::vector<OutputDirective> output_directives_;

  // 当使用--encode或者--decode的时候，codec_type_指明了encode或者decode的类型
  // - 如果codec_type_为空则表示--decode_raw类型;
  string codec_type_;

  // 如果指定了--descriptor_set_out选项，FileDescriptorSet将被输出到指定的文件
  string descriptor_set_name_;

  // If --dependency_out was given, this is the path to the file where the
  // dependency file will be written. Otherwise, empty.
  string dependency_out_name_;

  // Path to a file that contains serialized AccessInfo which provides
  // relative hotness of fields per message. This helps protoc to generate
  // better code.
  string profile_path_;

  // 如果指定了--include-imports那么所有的依赖proto都要写到DescriptorSet；
  // 如果未指定，则只把命令行中列出的proto文件写入；
  bool imports_in_descriptor_set_;

  // 如果指定--include_source_info为true，则不能从DescriptorSet中删除SourceCodeInfo
  bool source_info_in_descriptor_set_;

  // --disallow_services_这个选项有被使用吗？
  bool disallow_services_;

  // See SetInputsAreProtoPathRelative().
  bool inputs_are_proto_path_relative_;

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(CommandLineInterface);
};

}  // namespace compiler
}  // namespace protobuf

}  // namespace google
#endif  // GOOGLE_PROTOBUF_COMPILER_COMMAND_LINE_INTERFACE_H__
