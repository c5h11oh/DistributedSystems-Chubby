#include <google/protobuf/compiler/importer.h>

#include <cstdio>
#include <exception>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#include "build/_deps/grpc-src/third_party/protobuf/src/google/protobuf/descriptor.h"

class MockErrorCollector
    : public google::protobuf::compiler::MultiFileErrorCollector {
 public:
  MockErrorCollector() {}
  ~MockErrorCollector() override {}

  std::string text_;
  std::string warning_text_;

  // implements ErrorCollector ---------------------------------------
  void AddError(const std::string& filename, int line, int column,
                const std::string& message) override {
    std::cout << filename << " " << line << " " << column << " " << message
              << std::endl;
  }

  void AddWarning(const std::string& filename, int line, int column,
                  const std::string& message) override {
    std::cout << filename << " " << line << " " << column << " " << message
              << std::endl;
  }
};

namespace Type {
using namespace google::protobuf;

class BaseType {
 public:
  explicit BaseType(std::string type_name, std::string name, std::string nuname)
      : type_name(type_name), name(name), nuname(nuname) {}

  virtual std::string gen_decl() { return type_name + " " + name; }
  virtual std::string gen_size() { return "sizeof(" + type_name + ")"; }

  std::string type_name, name, nuname;
};

class StringType : public BaseType {
  using BaseType::BaseType;
  std::string gen_size() override {
    return "sizeof(" + name + ".size()) + " + name + ".size()";
  }
};

std::unique_ptr<BaseType> factory(FieldDescriptor::Type type_enum,
                                  std::string name) {
  switch (type_enum) {
    case FieldDescriptor::TYPE_INT64:
      return std::make_unique<BaseType>("int64_t", name, "i64");
    case FieldDescriptor::TYPE_INT32:
      return std::make_unique<BaseType>("int32_t", name, "i32");
    case FieldDescriptor::TYPE_STRING:
      return std::make_unique<StringType>("std::string", name, "str");
    default:
      std::cerr << "UNKNOWN TYPE" << std::endl;
      std::terminate();
  }
}

}  // namespace Type
void gen_code(const std::string& action_name,
              std::vector<std::unique_ptr<Type::BaseType>>& fields) {
  bool is_action = action_name.ends_with("Action");

  printf("struct %s {\n", action_name.c_str());

  if (is_action) {
    printf("const static ActionName action_name = ActionName::%s;",
           action_name.c_str());
  }
  for (auto& f : fields) std::cout << f->gen_decl() << ';' << std::endl;
  printf("explicit %s(", action_name.c_str());
  for (int i = 0; i < fields.size(); i++) {
    if (i != 0) putchar(',');
    std::cout << fields[i]->gen_decl();
  }
  putchar(')');
  if (!fields.empty()) putchar(':');
  for (int i = 0; i < fields.size(); i++) {
    if (i != 0) putchar(',');
    printf("%s(%s)", fields[i]->name.c_str(), fields[i]->name.c_str());
  }
  puts("{}");

  printf("%s(nuraft::buffer &data) {\n", action_name.c_str());
  puts("nuraft::buffer_serializer bs(data);");
  if (is_action) {
    puts("assert(bs.get_i8() == static_cast<int8_t>(action_name));");
  }
  for (auto& f : fields)
    printf("%s = bs.get_%s();\n", f->name.c_str(), f->nuname.c_str());
  puts("}");

  puts("nuraft::ptr<nuraft::buffer> serialize() const {");
  if (is_action) {
    printf(
        "nuraft::ptr<nuraft::buffer> buf = "
        "nuraft::buffer::alloc(sizeof(int8_t)");
  } else {
    printf("nuraft::ptr<nuraft::buffer> buf = nuraft::buffer::alloc(0");
  }
  for (int i = 0; i < fields.size(); i++) {
    putchar('+');
    std::cout << fields[i]->gen_size();
  }
  puts(");");
  puts("nuraft::buffer_serializer bs(buf);");
  if (is_action) {
    puts("bs.put_u8(static_cast<uint8_t>(action_name));");
  }
  for (auto& f : fields)
    printf("bs.put_%s(%s);\n", f->nuname.c_str(), f->name.c_str());
  puts("return buf;");
  puts("}};\n");
}

int main(int argc, char** argv) {
  std::cout << "#include <cstdint>\n"
               "#include <memory>\n"
               "#include <string>\n"
               "#include <variant>\n\n"

               "#include \"libnuraft/buffer.hxx\"\n"
               "#include \"libnuraft/buffer_serializer.hxx\"\n"
               "#include \"libnuraft/nuraft.hxx\"\n\n"

               "namespace action {\n\n"
            << std::endl;

  using namespace google::protobuf;
  compiler::DiskSourceTree dst;
  dst.MapPath("file", argv[1]);
  MockErrorCollector mfec;
  compiler::Importer importer(&dst, &mfec);
  importer.AddUnusedImportTrackFile("file");
  const FileDescriptor* file = importer.Import("file");
  assert(file != nullptr);

  std::vector<std::string> actions;
  for (int i = 0; i < file->message_type_count(); i++) {
    auto msg_type = file->message_type(i);
    if (msg_type->name().ends_with("Action"))
      actions.push_back(msg_type->name());
  }

  printf("enum class ActionName { ");
  for (int i = 0; i < actions.size(); i++) {
    if (i != 0) putchar(',');
    std::cout << actions[i];
  }
  puts("};");

  for (int i = 0; i < file->message_type_count(); i++) {
    auto msg_type = file->message_type(i);
    std::vector<std::unique_ptr<Type::BaseType>> fields;
    for (int j = 0; j < msg_type->field_count(); j++) {
      const auto& field = msg_type->field(j);
      fields.push_back(Type::factory(field->type(), field->name()));
    }
    gen_code(msg_type->name(), fields);
  }

  printf("std::variant<");
  for (int i = 0; i < actions.size(); i++) {
    if (i != 0) putchar(',');
    std::cout << actions[i];
  }
  puts("> create_action_from_buf(nuraft::buffer &data) {");
  puts("nuraft::buffer_serializer bs(data);");

  puts("switch (static_cast<ActionName>(bs.get_i8())) {");
  for (auto& s : actions) {
    printf("case ActionName::%s:\n", s.c_str());
    printf("return %s(data);", s.c_str());
  }
  puts("default:\nassert(0);\nstd::terminate();\n}}};");
}
