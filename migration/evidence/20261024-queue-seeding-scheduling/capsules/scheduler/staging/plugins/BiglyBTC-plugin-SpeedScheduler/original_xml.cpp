#include "scheduler.hpp"

#include <QByteArray>
#include <QString>
#include <QXmlStreamReader>

#include <charconv>

namespace biglybtc::plugins::speed_scheduler {
namespace {
struct Failure { std::string message; };
void require(bool value, std::string message) { if (!value) { throw Failure{std::move(message)}; } }
int number(const QXmlStreamAttributes& attributes, const QString& name) {
  require(attributes.hasAttribute(name), "Missing original schedule numeric attribute");
  const auto value = attributes.value(name).toUtf8();
  auto text = std::string_view(value.constData(), static_cast<std::size_t>(value.size()));
  if (text.starts_with('+')) {
    text.remove_prefix(1);
    require(!text.empty() && text.front() >= '0' && text.front() <= '9', "Invalid original schedule number");
  }
  int result{};
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  require(!text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "Invalid original schedule number");
  return result;
}
bool flag(const QXmlStreamAttributes& attributes, const QString& name) {
  return attributes.value(name).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}
int minute(const QXmlStreamAttributes& attributes) {
  const auto hour = number(attributes, QStringLiteral("hour"));
  const auto value = number(attributes, QStringLiteral("minute"));
  require(hour >= 0 && hour < 24 && value >= 0 && value < 60, "Invalid original schedule clock time");
  return hour * 60 + value;
}
}

std::expected<OriginalXml, Error> readOriginalXml(std::string input) {
  try {
    require(input.size() <= 4 * 1024 * 1024, "Original schedule XML exceeds its byte limit");
    QXmlStreamReader reader(QByteArray(input.data(), static_cast<qsizetype>(input.size())));
    Configuration result;
    std::optional<Schedule> current;
    std::size_t depth = 0, nodes = 0;
    bool root = false;
    while (!reader.atEnd()) {
      const auto token = reader.readNext();
      require(++nodes <= 100000, "Original schedule XML exceeds its token limit");
      require(token != QXmlStreamReader::DTD && token != QXmlStreamReader::EntityReference,
          "Schedule XML declarations/entities require explicit review");
      const auto name = reader.name();
      auto is = [&](const char* value) { return name.compare(QLatin1StringView(value), Qt::CaseInsensitive) == 0; };
      if (token == QXmlStreamReader::StartElement) {
        require(++depth <= 64, "Original schedule XML exceeds its depth limit");
        if (!root) {
          require(is("SpeedScheduler"), "Unknown original schedule root");
          root = true;
        }
        const auto attributes = reader.attributes();
        if (is("defaultRates")) {
          result.defaultUpload = number(attributes, QStringLiteral("upload"));
          result.defaultDownload = number(attributes, QStringLiteral("download"));
          if (attributes.hasAttribute(QStringLiteral("enforce"))) {
            result.defaultsEnforce = attributes.value(QStringLiteral("enforce")) == QStringLiteral("1");
          }
        } else if (is("schedule")) {
          require(!current && result.schedules.size() < MaximumSchedules, "Nested or excessive original schedules");
          current.emplace();
          current->enabled = attributes.value(QStringLiteral("enabled")).compare(QStringLiteral("false"), Qt::CaseInsensitive) != 0;
        } else if (current) {
          if (is("startTime")) { current->start = minute(attributes); }
          else if (is("endTime")) { current->end = minute(attributes); }
          else if (is("rates")) {
            current->upload = number(attributes, QStringLiteral("upload"));
            current->download = number(attributes, QStringLiteral("download"));
            current->downloadsPaused = flag(attributes, QStringLiteral("pauseDownloads"));
            current->seedsPaused = flag(attributes, QStringLiteral("pauseSeeds"));
            if (flag(attributes, QStringLiteral("pauseTransfers"))) { current->downloadsPaused = current->seedsPaused = true; }
          } else if (is("day")) {
            const auto index = number(attributes, QStringLiteral("index"));
            require(index >= 0 && index < 7, "Invalid original schedule day index");
            current->days[static_cast<std::size_t>(index)] = flag(attributes, QStringLiteral("selected"));
          } else if (is("category")) {
            current->category = attributes.hasAttribute(QStringLiteral("selected"))
                ? std::optional{attributes.value(QStringLiteral("selected")).toUtf8().toStdString()} : std::nullopt;
            require(!current->category || current->category->size() <= 65536, "Original category exceeds its byte limit");
          } else if (is("catSelection")) {
            current->excludeCategory = flag(attributes, QStringLiteral("not_in"));
            current->onlyCategory = flag(attributes, QStringLiteral("in"));
          }
        }
      } else if (token == QXmlStreamReader::EndElement) {
        if (is("schedule") && current) { result.schedules.push_back(std::move(*current)); current.reset(); }
        require(depth > 0, "Invalid original XML element balance");
        --depth;
      }
    }
    require(root && !reader.hasError() && depth == 0 && !current, "Malformed original schedule XML");
    return OriginalXml{std::move(result), std::move(input)};
  } catch (const Failure& failure) { return std::unexpected(Error{failure.message}); }
}
} // namespace biglybtc::plugins::speed_scheduler
