#include "tools/passwordless_sync_policy_xml.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits>
#include <optional>
#include <string>

namespace greeter::passwordless_sync::detail {

  namespace {

    constexpr std::string_view kActionId = "org.noctalia.greeter.sync-appearance";
    constexpr std::string_view kExecPathAnnotation = "org.freedesktop.policykit.exec.path";
    constexpr std::string_view kExecArgv1Annotation = "org.freedesktop.policykit.exec.argv1";

    [[nodiscard]] bool xmlNodeNamed(const xmlNode* node, const char* name) {
      return node != nullptr
          && node->type == XML_ELEMENT_NODE
          && node->ns == nullptr
          && ::xmlStrEqual(node->name, BAD_CAST name) != 0;
    }

    [[nodiscard]] std::optional<std::string> xmlProperty(xmlNode* node, const char* name) {
      xmlChar* raw = ::xmlGetProp(node, BAD_CAST name);
      if (raw == nullptr) {
        return std::nullopt;
      }
      std::string value(reinterpret_cast<const char*>(raw));
      ::xmlFree(raw);
      return value;
    }

    [[nodiscard]] std::optional<std::string> xmlDirectText(const xmlNode* node) {
      std::string value;
      for (const xmlNode* child = node->children; child != nullptr; child = child->next) {
        if (child->type == XML_COMMENT_NODE) {
          continue;
        }
        if (child->type != XML_TEXT_NODE && child->type != XML_CDATA_SECTION_NODE) {
          return std::nullopt;
        }
        if (child->content != nullptr) {
          value += reinterpret_cast<const char*>(child->content);
        }
      }
      return value;
    }

    [[nodiscard]] bool validateDocument(xmlDoc* document, const std::filesystem::path& expectedHelper) {
      xmlNode* root = ::xmlDocGetRootElement(document);
      if (!xmlNodeNamed(root, "policyconfig")) {
        return false;
      }

      std::size_t actionCount = 0;
      std::size_t helperAnnotationCount = 0;
      std::size_t argumentAnnotationCount = 0;
      std::optional<std::string> annotatedHelper;
      std::optional<std::string> annotatedArgument;

      for (xmlNode* action = root->children; action != nullptr; action = action->next) {
        if (!xmlNodeNamed(action, "action")) {
          continue;
        }
        const auto id = xmlProperty(action, "id");
        if (!id.has_value() || *id != kActionId) {
          continue;
        }
        ++actionCount;

        for (xmlNode* annotation = action->children; annotation != nullptr; annotation = annotation->next) {
          if (!xmlNodeNamed(annotation, "annotate")) {
            continue;
          }
          const auto key = xmlProperty(annotation, "key");
          if (!key.has_value()) {
            continue;
          }
          if (*key == kExecPathAnnotation) {
            ++helperAnnotationCount;
            annotatedHelper = xmlDirectText(annotation);
          } else if (*key == kExecArgv1Annotation) {
            ++argumentAnnotationCount;
            annotatedArgument = xmlDirectText(annotation);
          }
        }
      }

      return actionCount == 1
          && helperAnnotationCount == 1
          && argumentAnnotationCount == 1
          && annotatedHelper.has_value()
          && *annotatedHelper == expectedHelper.string()
          && annotatedArgument.has_value()
          && *annotatedArgument == "--sync";
    }

  } // namespace

  bool validateConstrainedPolicyXml(const std::string_view contents, const std::filesystem::path& expectedHelper) {
    if (contents.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      return false;
    }
    constexpr int parseOptions = XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_COMPACT;
    xmlDoc* document = ::xmlReadMemory(
        contents.data(), static_cast<int>(contents.size()), "noctalia-greeter-policy.xml", nullptr, parseOptions
    );
    if (document == nullptr) {
      return false;
    }
    const bool valid = validateDocument(document, expectedHelper);
    ::xmlFreeDoc(document);
    return valid;
  }

} // namespace greeter::passwordless_sync::detail
