/*
 * Copyright 2016 Google Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "kythe/cxx/doc/html_renderer.h"
#include "kythe/cxx/doc/markup_handler.h"

#include <stack>

namespace kythe {
namespace {
/// \brief A RAII class to deal with styled div/span tags.
class CssTag {
 public:
  enum class Kind { Div, Span };
  /// \param kind the kind of tag to open.
  /// \param style the CSS style to apply to the tag. Must be escaped.
  /// \param buffer must outlive CssTag.
  CssTag(Kind kind, const std::string& style, std::string* buffer)
      : kind_(kind), buffer_(buffer) {
    buffer_->append("<");
    buffer_->append(label());
    buffer_->append(" class=\"");
    buffer_->append(style);
    buffer_->append("\">");
  }
  ~CssTag() {
    buffer_->append("</");
    buffer_->append(label());
    buffer_->append(">");
  }
  const char* label() const { return kind_ == Kind::Div ? "div" : "span"; }
  CssTag(const CssTag& o) = delete;

 private:
  Kind kind_;
  std::string* buffer_;
};

std::string RenderPrintable(const HtmlRendererOptions& options,
                            const std::vector<MarkupHandler>& handlers,
                            const proto::Printable& printable_proto,
                            Printable::RejectPolicy filter) {
  Printable printable(printable_proto, filter);
  auto markdoc = HandleMarkup(handlers, printable);
  return RenderHtml(options, markdoc);
}

/// \brief Appends a representation of `c` to `buffer`, possibly using an HTML
/// entity instead of a literal character.
void AppendEscapedHtmlCharacter(std::string* buffer, char c) {
  switch (c) {
    case '<':
      buffer->append("&lt;");
      break;
    case '>':
      buffer->append("&gt;");
      break;
    case '&':
      buffer->append("&amp;");
      break;
    default:
      buffer->push_back(c);
  }
}

/// \brief Content of a tagged block (e.g., a @param or a @returns).
struct TaggedText {
  std::string buffer;
};

/// \brief A map from tag block IDs and ordinals to their content.
using TagBlocks =
    std::map<std::pair<PrintableSpan::TagBlockId, size_t>, TaggedText>;

/// \brief Renders the content of `tag_blocks` to the block `out`.
void RenderTagBlocks(const HtmlRendererOptions& options,
                     const TagBlocks& tag_blocks, TaggedText* out) {
  for (const auto& block : tag_blocks) {
    {
      CssTag title(CssTag::Kind::Div, options.tag_section_title_div,
                   &out->buffer);
      // TODO(zarko): Aggregate tag blocks by ID.
      switch (block.first.first) {
        case PrintableSpan::TagBlockId::Author:
          out->buffer.append("Author");
          break;
        case PrintableSpan::TagBlockId::Returns:
          out->buffer.append("Returns");
          break;
        case PrintableSpan::TagBlockId::Since:
          out->buffer.append("Since");
          break;
        case PrintableSpan::TagBlockId::Version:
          out->buffer.append("Version");
          break;
        case PrintableSpan::TagBlockId::Throws:
          out->buffer.append("Throws");
          break;
        case PrintableSpan::TagBlockId::Param:
          out->buffer.append("Parameter");
          break;
      }
    }
    {
      CssTag content(CssTag::Kind::Div, options.tag_section_content_div,
                     &out->buffer);
      out->buffer.append(block.second.buffer);
    }
  }
}

const char* TagNameForStyle(PrintableSpan::Style style) {
  switch (style) {
    case PrintableSpan::Style::Bold:
      return "b";
    case PrintableSpan::Style::Italic:
      return "i";
    case PrintableSpan::Style::H1:
      return "h1";
    case PrintableSpan::Style::H2:
      return "h2";
    case PrintableSpan::Style::H3:
      return "h3";
    case PrintableSpan::Style::H4:
      return "h4";
    case PrintableSpan::Style::H5:
      return "h5";
    case PrintableSpan::Style::H6:
      return "h6";
  }
}
}  // anonymous namespace

std::string RenderHtml(const HtmlRendererOptions& options,
                       const Printable& printable) {
  struct OpenSpan {
    const PrintableSpan* span;
    bool valid;
  };
  struct FormatState {
    bool in_pre_block;
  };
  std::stack<OpenSpan> open_spans;
  // To avoid entering multiple <pre> blocks, we keep track of whether we're
  // currently in a <pre> context. This does not affect escaping, since
  // tags can appear in a <pre>.
  std::stack<FormatState> format_states;
  // Elements on `open_tags` point to values of `tag_blocks`. The element on
  // top of the stack is the tag block whose buffer we're currently appending
  // data to (if any). This stack should usually have one or zero elements,
  // given the syntactic restrictions of the markup languages we're translating
  // from.
  std::stack<TaggedText*> open_tags;
  std::map<std::pair<PrintableSpan::TagBlockId, size_t>, TaggedText> tag_blocks;
  TaggedText main_text;
  // `out` points to either `main_text` if `open_tags` is empty or a value of
  // `tag_blocks` (particularly, the one referenced by the top of `open_tags`)
  // if the stack is non-empty.
  TaggedText* out = &main_text;
  PrintableSpan default_span(0, printable.text().size(),
                             PrintableSpan::Semantic::Raw);
  open_spans.push(OpenSpan{&default_span, true});
  format_states.push(FormatState{false});
  size_t current_span = 0;
  for (size_t i = 0; i <= printable.text().size(); ++i) {
    // Normalized PrintableSpans have all empty or negative-length spans
    // dropped.
    while (!open_spans.empty() && open_spans.top().span->end() == i) {
      switch (open_spans.top().span->semantic()) {
        case PrintableSpan::Semantic::TagBlock: {
          if (!open_tags.empty()) {
            open_tags.pop();
          }
          out = open_tags.empty() ? &main_text : open_tags.top();
        } break;
        case PrintableSpan::Semantic::UriLink:
          out->buffer.append("</a>");
          break;
        case PrintableSpan::Semantic::Uri:
          out->buffer.append("\">");
          break;
        case PrintableSpan::Semantic::Link:
          if (open_spans.top().valid) {
            out->buffer.append("</a>");
          }
          break;
        case PrintableSpan::Semantic::CodeRef:
          out->buffer.append("</tt>");
          break;
        case PrintableSpan::Semantic::Paragraph:
          out->buffer.append("</p>");
          break;
        case PrintableSpan::Semantic::ListItem:
          out->buffer.append("</li>");
          break;
        case PrintableSpan::Semantic::UnorderedList:
          out->buffer.append("</ul>");
          break;
        case PrintableSpan::Semantic::Styled:
          out->buffer.append("</");
          out->buffer.append(TagNameForStyle(open_spans.top().span->style()));
          out->buffer.append(">");
          break;
        case PrintableSpan::Semantic::CodeBlock:
          if (!format_states.empty()) {
            format_states.pop();
            if (!format_states.empty() && !format_states.top().in_pre_block) {
              out->buffer.append("</pre>");
            }
          }
          break;
        default:
          break;
      }
      open_spans.pop();
    }
    if (open_spans.empty() || i == printable.text().size()) {
      // default_span is first to enter and last to leave; there also may
      // be no empty spans.
      break;
    }
    while (current_span < printable.spans().size() &&
           printable.spans().span(current_span).begin() == i) {
      open_spans.push({&printable.spans().span(current_span), true});
      ++current_span;
      switch (open_spans.top().span->semantic()) {
        case PrintableSpan::Semantic::TagBlock: {
          auto block = open_spans.top().span->tag_block();
          out = &tag_blocks[block];
          open_tags.push(out);
        } break;
        case PrintableSpan::Semantic::UriLink:
          out->buffer.append("<a ");
          break;
        case PrintableSpan::Semantic::Uri:
          out->buffer.append("href=\"");
          break;
        case PrintableSpan::Semantic::Link:
          open_spans.top().valid = false;  // Invalid until proven otherwise.
          if (open_spans.top().span->link().definition_size() != 0) {
            const auto& definition =
                open_spans.top().span->link().definition(0);
            if (const auto* def_info = options.node_info(definition)) {
              if (!def_info->definition().empty()) {
                if (const auto* def_anchor =
                        options.anchor_for_ticket(def_info->definition())) {
                  open_spans.top().valid = true;
                  out->buffer.append("<a href=\"");
                  auto link_uri = options.make_link_uri(*def_anchor);
                  // + 2 for the closing ">.
                  out->buffer.reserve(out->buffer.size() + link_uri.size() + 2);
                  for (auto c : link_uri) {
                    AppendEscapedHtmlCharacter(&out->buffer, c);
                  }
                  out->buffer.append("\">");
                }
              }
            }
          }
          break;
        case PrintableSpan::Semantic::CodeRef:
          out->buffer.append("<tt>");
          break;
        case PrintableSpan::Semantic::Paragraph:
          out->buffer.append("<p>");
          break;
        case PrintableSpan::Semantic::ListItem:
          out->buffer.append("<li>");
          break;
        case PrintableSpan::Semantic::UnorderedList:
          out->buffer.append("<ul>");
          break;
        case PrintableSpan::Semantic::Styled:
          out->buffer.append("<");
          out->buffer.append(TagNameForStyle(open_spans.top().span->style()));
          out->buffer.append(">");
          break;
        case PrintableSpan::Semantic::CodeBlock:
          if (!format_states.empty() && !format_states.top().in_pre_block) {
            out->buffer.append("<pre>");
          }
          format_states.push(FormatState{true});
          break;
        default:
          break;
      }
    }
    if (open_spans.top().span->semantic() == PrintableSpan::Semantic::Escaped) {
      out->buffer.push_back(printable.text()[i]);
    } else if (open_spans.top().span->semantic() !=
               PrintableSpan::Semantic::Markup) {
      char c = printable.text()[i];
      AppendEscapedHtmlCharacter(&out->buffer, c);
    }
  }
  RenderTagBlocks(options, tag_blocks, out);
  return out->buffer;
}

std::string RenderDocument(
    const HtmlRendererOptions& options,
    const std::vector<MarkupHandler>& handlers,
    const proto::DocumentationReply::Document& document) {
  std::string text_out;
  {
    CssTag root(CssTag::Kind::Div, options.doc_div, &text_out);
    {
      CssTag sig(CssTag::Kind::Div, options.signature_div, &text_out);
      {
        CssTag type_div(CssTag::Kind::Div, options.type_name_div, &text_out);
        CssTag type_name(CssTag::Kind::Span, options.name_span, &text_out);
        text_out.append(RenderPrintable(
            options, {}, document.signature(),
            Printable::RejectUnimportant | Printable::IncludeLists));
      }
      {
        CssTag detail_div(CssTag::Kind::Div, options.sig_detail_div, &text_out);
        if (const auto* doc_info = options.node_info(document.ticket())) {
          auto kind = doc_info->facts().find(document.ticket());
          if (kind != doc_info->facts().end()) {
            for (char c : kind->second) {
              AppendEscapedHtmlCharacter(&text_out, c);
            }
            text_out.append(" ");
          }
        }
        text_out.append("declared by ");
        text_out.append(RenderPrintable(options, {}, document.defined_by(),
                                        Printable::IncludeAll));
      }
    }
    text_out.append(RenderPrintable(options, handlers, document.text(),
                                    Printable::IncludeAll));
  }
  return text_out;
}

}  // namespace kythe
