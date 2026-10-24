#include "scheduler_service.hpp"

#include <QByteArray>
#include <QString>
#include <QXmlStreamReader>

#include <algorithm>
#include <charconv>
#include <map>
#include <set>

namespace biglybtc::storage {
namespace {
struct Failure { Error error; };
void require(bool value, const char* reason) { if (!value) { throw Failure{{ErrorCode::InvalidFormat, reason}}; } }
struct Attr { std::string name; std::size_t prefix, value, valueEnd, end; };
struct Tag {
  std::string name, qualified;
  std::size_t begin = 0, end = 0, partner = 0;
  bool closing = false, self = false;
  std::vector<Attr> attributes;
  std::map<std::string, std::string, std::less<>> decoded;
};
struct Patch { std::size_t begin, end; std::string value; };
struct Xml {
  std::string raw;
  int width = 1; bool little = true;
  std::vector<Tag> tags;
  std::vector<std::size_t> schedules;
  std::vector<Patch> patches;
  std::map<std::size_t, std::string> inserts;
  std::map<std::size_t, std::string> expand;
  explicit Xml(std::string value) : raw(std::move(value)) {
    if (raw.starts_with("\xff\xfe") || (raw.size() >= 2 && raw[0] == '<' && raw[1] == 0)) { width = 2; little = true; }
    else if (raw.starts_with("\xfe\xff") || (raw.size() >= 2 && raw[0] == 0 && raw[1] == '<')) { width = 2; little = false; }
  }
  unsigned int at(std::size_t pos) const {
    if (pos >= raw.size()) { return 0; }
    if (width == 1) { return static_cast<unsigned char>(raw[pos]); }
    require(pos + 1 < raw.size(), "Truncated UTF16 schedule source");
    const auto a = static_cast<unsigned char>(raw[pos]), b = static_cast<unsigned char>(raw[pos + 1]);
    return little ? a | (b << 8) : (a << 8) | b;
  }
  bool is(std::size_t pos, std::string_view value) const {
    for (const auto c : value) { if (at(pos) != static_cast<unsigned char>(c)) { return false; } pos += width; }
    return true;
  }
  std::size_t until(std::size_t pos, std::string_view end) const {
    while (pos < raw.size()) { if (is(pos, end)) { return pos + end.size() * width; } pos += width; }
    require(false, "Unterminated XML token"); return 0;
  }
  bool space(std::size_t pos) const { const auto c = at(pos); return c == 32 || c == 9 || c == 10 || c == 13; }
  std::string name(std::size_t& pos) const {
    std::string value;
    while (pos < raw.size()) {
      const auto c = at(pos);
      if (space(pos) || c == '/' || c == '>' || c == '=' || c == '?' || c == '\'' || c == '"') { break; }
      require(c > 0 && c <= 127, "Non-ASCII XML names require explicit editing support");
      value.push_back(static_cast<char>(c)); pos += width;
    }
    require(!value.empty(), "Missing XML name"); return value;
  }
  static std::string local(std::string value) {
    const auto colon = value.rfind(':'); if (colon != std::string::npos) { value.erase(0, colon + 1); }
    for (auto& c : value) { if (c >= 'A' && c <= 'Z') { c += 'a' - 'A'; } }
    return value;
  }
  std::string encode(std::string_view value) const {
    const auto decoded = QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    require(decoded.toUtf8().toStdString() == value, "Schedule edits must use valid UTF8 text");
    if (width == 1) { return std::string(value); }
    std::string output; output.reserve(static_cast<std::size_t>(decoded.size()) * 2);
    for (const auto c : decoded) {
      const auto unit = c.unicode();
      output.push_back(static_cast<char>(little ? unit & 255 : unit >> 8));
      output.push_back(static_cast<char>(little ? unit >> 8 : unit & 255));
    }
    return output;
  }
  void scan() {
    std::vector<std::size_t> stack;
    std::size_t pos = width == 2 && (raw.starts_with("\xff\xfe") || raw.starts_with("\xfe\xff")) ? 2 : 0;
    if (width == 1 && raw.starts_with("\xef\xbb\xbf")) { pos = 3; }
    while (pos < raw.size()) {
      if (at(pos) != '<') { pos += width; continue; }
      if (is(pos, "<!--")) { pos = until(pos + 4 * width, "-->"); continue; }
      if (is(pos, "<![CDATA[")) { pos = until(pos + 9 * width, "]]>"); continue; }
      if (is(pos, "<?")) { pos = until(pos + 2 * width, "?>"); continue; }
      require(!is(pos, "<!"), "Unsupported XML declaration while editing");
      Tag tag; tag.begin = pos; pos += width;
      tag.closing = at(pos) == '/'; if (tag.closing) { pos += width; }
      tag.qualified = name(pos); tag.name = local(tag.qualified);
      while (true) {
        const auto prefix = pos; while (space(pos)) { pos += width; }
        if (at(pos) == '>') { pos += width; break; }
        if (is(pos, "/>")) { tag.self = true; pos += 2 * width; break; }
        require(!tag.closing, "Unexpected end-tag attributes");
        Attr attr; attr.prefix = prefix; attr.name = name(pos);
        while (space(pos)) { pos += width; } require(at(pos) == '=', "Missing XML attribute assignment"); pos += width;
        while (space(pos)) { pos += width; } const auto quote = at(pos); require(quote == '\'' || quote == '"', "Unquoted XML attribute"); pos += width;
        attr.value = pos; while (pos < raw.size() && at(pos) != quote) { pos += width; }
        require(pos < raw.size(), "Unterminated XML attribute"); attr.valueEnd = pos; pos += width; attr.end = pos;
        tag.attributes.push_back(std::move(attr));
      }
      tag.end = pos; const auto index = tags.size(); tags.push_back(std::move(tag));
      if (tags.back().closing) {
        require(!stack.empty() && tags[stack.back()].qualified == tags.back().qualified, "Ambiguous XML nesting");
        tags[stack.back()].partner = index; tags.back().partner = stack.back(); stack.pop_back();
      } else if (tags.back().self) { tags.back().partner = index; }
      else { stack.push_back(index); }
      if (!tags.back().closing && tags.back().name == "schedule") { schedules.push_back(index); }
    }
    require(stack.empty(), "Unclosed XML elements");
    QXmlStreamReader reader(QByteArray(raw.data(), static_cast<qsizetype>(raw.size())));
    std::size_t index = 0;
    while (!reader.atEnd()) {
      if (reader.readNext() != QXmlStreamReader::StartElement) { continue; }
      const auto encoding = reader.documentEncoding().toString().toLower();
      require(encoding.isEmpty() || encoding == QStringLiteral("utf-8") || encoding == QStringLiteral("utf8")
          || encoding == QStringLiteral("utf-16") || encoding == QStringLiteral("utf-16le") || encoding == QStringLiteral("utf-16be"),
          "Editing this XML character encoding is unsupported");
      while (index < tags.size() && tags[index].closing) { ++index; }
      require(index < tags.size(), "XML token map differs from parser");
      for (const auto& attr : reader.attributes()) { tags[index].decoded.emplace(attr.qualifiedName().toUtf8().toStdString(), attr.value().toUtf8().toStdString()); }
      ++index;
    }
    require(!reader.hasError(), "Invalid original XML while editing");
  }
  static std::string escape(std::string_view input) {
    std::string output;
    for (const auto c : input) {
      switch (c) { case '&': output += "&amp;"; break; case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break; case '"': output += "&quot;"; break; case '\'': output += "&apos;"; break;
        default: output.push_back(c); }
    }
    return output;
  }
  void attribute(std::size_t index, std::string_view key, std::optional<std::string> value) {
    const auto& tag = tags[index];
    const auto found = std::find_if(tag.attributes.begin(), tag.attributes.end(), [&](const auto& attr) { return attr.name == key; });
    if (found != tag.attributes.end()) {
      if (value) { patches.push_back({found->value, found->valueEnd, encode(escape(*value))}); }
      else { patches.push_back({found->prefix, found->end, {}}); }
    } else if (value) {
      inserts[tag.end - (tag.self ? 2 : 1) * width] += encode(" " + std::string(key) + "=\"" + escape(*value) + "\"");
    }
  }
  void inside(std::size_t index, std::string value) {
    const auto& tag = tags[index];
    if (tag.self) { expand[index] += encode(value); }
    else { inserts[tags[tag.partner].begin] += encode(value); }
  }
  std::optional<std::size_t> unique(std::size_t first, std::size_t last, std::string_view wanted) const {
    std::optional<std::size_t> result;
    for (auto i = first; i <= last; ++i) {
      if (!tags[i].closing && tags[i].name == wanted) { require(!result, "Duplicate known XML elements require explicit editing support"); result = i; }
    }
    return result;
  }
  void element(std::size_t schedule, std::string_view key, const std::map<std::string, std::string>& attributes,
      std::string canonical) {
    const auto found = unique(schedule + 1, tags[schedule].partner, key);
    if (!found) { inside(schedule, std::move(canonical)); return; }
    for (const auto& [name, value] : attributes) { attribute(*found, name, value); }
  }
  std::string finish() {
    for (const auto& [index, contents] : expand) {
      const auto& tag = tags[index]; patches.push_back({tag.end - 2 * width, tag.end, encode(">") + contents + encode("</" + tag.qualified + ">")});
    }
    for (auto& [pos, value] : inserts) { patches.push_back({pos, pos, std::move(value)}); }
    std::ranges::sort(patches, [](const auto& a, const auto& b) { return a.begin < b.begin || (a.begin == b.begin && a.end < b.end); });
    std::string output; std::size_t position = 0;
    for (const auto& patch : patches) {
      require(patch.begin >= position && patch.end >= patch.begin && patch.end <= raw.size(), "Overlapping XML edit intents");
      output.append(raw, position, patch.begin - position); output += patch.value; position = patch.end;
      require(output.size() <= 4 * 1024 * 1024, "Edited schedule XML exceeds its bound");
    }
    output.append(raw, position, raw.size() - position); return output;
  }
};
std::string boolean(bool value) { return value ? "true" : "false"; }
std::string scheduleText(const scheduler::Schedule& value) {
  require(value.start >= 0 && value.start < 1440 && value.end >= 0 && value.end < 1440
      && value.upload >= 0 && value.download >= 0, "Invalid appended schedule time or rate");
  std::string result = "\n      <schedule enabled=\"" + boolean(value.enabled) + "\">\n"
      "         <startTime hour=\"" + std::to_string(value.start / 60) + "\" minute=\"" + std::to_string(value.start % 60) + "\"/>\n"
      "         <endTime hour=\"" + std::to_string(value.end / 60) + "\" minute=\"" + std::to_string(value.end % 60) + "\"/>\n"
      "         <rates upload=\"" + std::to_string(value.upload) + "\" download=\"" + std::to_string(value.download)
      + "\" pauseDownloads=\"" + boolean(value.downloadsPaused) + "\" pauseSeeds=\"" + boolean(value.seedsPaused) + "\"/>\n         <days>";
  for (std::size_t i = 0; i < 7; ++i) { result += "<day index=\"" + std::to_string(i) + "\" selected=\"" + boolean(value.days[i]) + "\"/>"; }
  result += "</days>\n         <category";
  if (value.category) { result += " selected=\"" + Xml::escape(*value.category) + "\""; }
  return result + "/>\n         <catSelection not_in=\"" + boolean(value.excludeCategory) + "\" in=\"" + boolean(value.onlyCategory) + "\"/>\n      </schedule>\n";
}
void updateSchedule(Xml& xml, std::size_t tag, const scheduler::Schedule& before, const scheduler::Schedule& after) {
  require(after.start >= 0 && after.start < 1440 && after.end >= 0 && after.end < 1440
      && after.upload >= 0 && after.download >= 0, "Invalid edited schedule time or rate");
  if (before.enabled != after.enabled) { xml.attribute(tag, "enabled", boolean(after.enabled)); }
  const auto time = [&](const char* name, int a, int b) {
    if (a == b) { return; } std::map<std::string, std::string> changes;
    if (a / 60 != b / 60) { changes["hour"] = std::to_string(b / 60); }
    if (a % 60 != b % 60) { changes["minute"] = std::to_string(b % 60); }
    xml.element(tag, name, changes, "<" + std::string(name) + " hour=\"" + std::to_string(b / 60) + "\" minute=\"" + std::to_string(b % 60) + "\"/>");
  };
  time("starttime", before.start, after.start); time("endtime", before.end, after.end);
  std::map<std::string, std::string> rates;
  if (before.upload != after.upload) { rates["upload"] = std::to_string(after.upload); }
  if (before.download != after.download) { rates["download"] = std::to_string(after.download); }
  if (before.downloadsPaused != after.downloadsPaused || before.seedsPaused != after.seedsPaused) {
    rates["pauseDownloads"] = boolean(after.downloadsPaused); rates["pauseSeeds"] = boolean(after.seedsPaused);
    const auto found = xml.unique(tag + 1, xml.tags[tag].partner, "rates");
    if (found && xml.tags[*found].decoded.contains("pauseTransfers")) { rates["pauseTransfers"] = "false"; }
  }
  if (!rates.empty()) { xml.element(tag, "rates", rates, "<rates upload=\"" + std::to_string(after.upload) + "\" download=\"" + std::to_string(after.download)
      + "\" pauseDownloads=\"" + boolean(after.downloadsPaused) + "\" pauseSeeds=\"" + boolean(after.seedsPaused) + "\"/>"); }
  for (std::size_t day = 0; day < 7; ++day) {
    if (before.days[day] == after.days[day]) { continue; }
    std::optional<std::size_t> found;
    for (auto i = tag + 1; i < xml.tags[tag].partner; ++i) {
      const auto& item = xml.tags[i]; if (item.closing || item.name != "day") { continue; }
      const auto index = item.decoded.find("index"); require(index != item.decoded.end(), "Missing day index");
      auto value = std::string_view(index->second); if (value.starts_with('+')) { value.remove_prefix(1); }
      int number = -1; const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
      require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size(), "Invalid day index");
      if (number == static_cast<int>(day)) { require(!found, "Duplicate day index is ambiguous for editing"); found = i; }
    }
    if (found) { xml.attribute(*found, "selected", boolean(after.days[day])); }
    else {
      const auto days = xml.unique(tag + 1, xml.tags[tag].partner, "days");
      const auto value = "<day index=\"" + std::to_string(day) + "\" selected=\"" + boolean(after.days[day]) + "\"/>";
      if (days) { xml.inside(*days, value); } else { xml.inside(tag, "<days>" + value + "</days>"); }
    }
  }
  if (before.category != after.category) {
    const auto found = xml.unique(tag + 1, xml.tags[tag].partner, "category");
    if (found) { xml.attribute(*found, "selected", after.category); }
    else { xml.inside(tag, after.category ? "<category selected=\"" + Xml::escape(*after.category) + "\"/>" : "<category/>"); }
  }
  if (before.excludeCategory != after.excludeCategory || before.onlyCategory != after.onlyCategory) {
    xml.element(tag, "catselection", {{"not_in", boolean(after.excludeCategory)}, {"in", boolean(after.onlyCategory)}},
        "<catSelection not_in=\"" + boolean(after.excludeCategory) + "\" in=\"" + boolean(after.onlyCategory) + "\"/>");
  }
}
}
std::expected<std::string, Error> patchSchedulerXml(std::string_view source, const SchedulerXmlChanges& changes) {
  try {
    require(changes.update.size() <= scheduler::MaximumSchedules && changes.remove.size() <= scheduler::MaximumSchedules
        && changes.append.size() <= scheduler::MaximumSchedules, "Too many schedule edits");
    Xml xml(source.empty() ? "<?xml version=\"1.0\" encoding=\"UTF-8\"?><SpeedScheduler><defaultRates upload=\"0\" download=\"0\" enforce=\"1\"/><schedules/></SpeedScheduler>" : std::string(source));
    auto parsed = scheduler::readOriginalXml(xml.raw); require(parsed.has_value(), "Invalid original schedule XML");
    auto expected = parsed->configuration; xml.scan(); require(xml.schedules.size() == expected.schedules.size(), "Schedule index map differs from parser");
    const auto root = xml.unique(0, xml.tags.size() - 1, "speedscheduler"); require(root.has_value(), "Missing schedule root");
    std::set<std::size_t> removed, updated;
    for (const auto index : changes.remove) {
      require(index < expected.schedules.size() && removed.insert(index).second, "Duplicate or invalid removed schedule index");
      const auto tag = xml.schedules[index]; xml.patches.push_back({xml.tags[tag].begin, xml.tags[xml.tags[tag].partner].end, {}});
    }
    for (const auto& item : changes.update) {
      require(item.sourceIndex < expected.schedules.size() && !removed.contains(item.sourceIndex) && updated.insert(item.sourceIndex).second,
          "Duplicate or conflicting edited schedule index");
      updateSchedule(xml, xml.schedules[item.sourceIndex], expected.schedules[item.sourceIndex], item.value);
      expected.schedules[item.sourceIndex] = item.value;
    }
    for (auto it = removed.rbegin(); it != removed.rend(); ++it) { expected.schedules.erase(expected.schedules.begin() + static_cast<std::ptrdiff_t>(*it)); }
    if (changes.defaults) {
      const auto& value = *changes.defaults; require(value.upload >= 0 && value.download >= 0, "Invalid default rates");
      const auto tag = xml.unique(0, xml.tags.size() - 1, "defaultrates");
      if (tag) {
        if (value.upload != expected.defaultUpload) { xml.attribute(*tag, "upload", std::to_string(value.upload)); }
        if (value.download != expected.defaultDownload) { xml.attribute(*tag, "download", std::to_string(value.download)); }
        if (value.enforce != expected.defaultsEnforce) { xml.attribute(*tag, "enforce", value.enforce ? "1" : "0"); }
      } else { xml.inside(*root, "<defaultRates upload=\"" + std::to_string(value.upload) + "\" download=\"" + std::to_string(value.download) + "\" enforce=\"" + (value.enforce ? "1" : "0") + "\"/>"); }
      expected.defaultUpload = value.upload; expected.defaultDownload = value.download; expected.defaultsEnforce = value.enforce;
    }
    require(changes.append.size() <= scheduler::MaximumSchedules - expected.schedules.size(), "Schedule count exceeds its bound");
    if (!changes.append.empty()) {
      std::string values; for (const auto& value : changes.append) { values += scheduleText(value); expected.schedules.push_back(value); }
      const auto tag = xml.unique(0, xml.tags.size() - 1, "schedules");
      if (tag) { xml.inside(*tag, std::move(values)); } else { xml.inside(*root, "<schedules>" + values + "</schedules>"); }
    }
    auto output = xml.finish(); require(output.size() <= 4 * 1024 * 1024, "Edited XML exceeds its byte limit");
    auto checked = scheduler::readOriginalXml(output);
    require(checked && checked->configuration == expected, "Lossless edits do not reproduce the requested original model");
    return output;
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::bad_alloc&) { return std::unexpected(Error{ErrorCode::Io, "Insufficient memory for schedule edits"}); }
}
} // namespace biglybtc::storage
