// Literal expectations copied from the actual original Java stdout.
// No native mirror produced numeric expected outputs.

void javaRateCase1() {
  std::string_view context = "empty-and-lazy-zero";
  auto created = SourceRateAverage::create(1000, 10, 100000LL);
  require(created.has_value(), context);
  auto subject = std::move(*created);
  context = "empty-and-lazy-zero:0:average";
  { const auto result = subject.average(100000LL);
    require(result && *result == 0LL, context); }
  context = "empty-and-lazy-zero:1:sum";
  { const auto result = subject.sum(101000LL);
    require(result && *result == 0LL, context); }
  context = "empty-and-lazy-zero:2:add";
  require(subject.add(0LL, 110000LL).has_value(), context);
  context = "empty-and-lazy-zero:3:point";
  { const auto result = subject.point(110000LL);
    require(result && *result == 0LL, context); }
  context = "empty-and-lazy-zero:4:average";
  { const auto result = subject.average(111000LL);
    require(result && *result == 0LL, context); }
}

void javaRateCase2() {
  std::string_view context = "current-excluded-warmup-decay";
  auto created = SourceRateAverage::create(1000, 10, 100000LL);
  require(created.has_value(), context);
  auto subject = std::move(*created);
  context = "current-excluded-warmup-decay:0:add";
  require(subject.add(1000LL, 100000LL).has_value(), context);
  context = "current-excluded-warmup-decay:1:average";
  { const auto result = subject.average(100999LL);
    require(result && *result == 0LL, context); }
  context = "current-excluded-warmup-decay:2:average";
  { const auto result = subject.average(101000LL);
    require(result && *result == 100LL, context); }
  context = "current-excluded-warmup-decay:3:point";
  { const auto result = subject.point(101000LL);
    require(result && *result == 1000LL, context); }
  context = "current-excluded-warmup-decay:4:average";
  { const auto result = subject.average(110000LL);
    require(result && *result == 100LL, context); }
  context = "current-excluded-warmup-decay:5:average";
  { const auto result = subject.average(111000LL);
    require(result && *result == 0LL, context); }
}

void javaRateCase3() {
  std::string_view context = "multiple-current-values";
  auto created = SourceRateAverage::create(1000, 10, 100000LL);
  require(created.has_value(), context);
  auto subject = std::move(*created);
  context = "multiple-current-values:0:add";
  require(subject.add(100LL, 100000LL).has_value(), context);
  context = "multiple-current-values:1:add";
  require(subject.add(200LL, 100900LL).has_value(), context);
  context = "multiple-current-values:2:add";
  require(subject.add(400LL, 101000LL).has_value(), context);
  context = "multiple-current-values:3:sum";
  { const auto result = subject.sum(101000LL);
    require(result && *result == 300LL, context); }
  context = "multiple-current-values:4:sum";
  { const auto result = subject.sum(102000LL);
    require(result && *result == 700LL, context); }
}

void javaRateCase4() {
  std::string_view context = "signed-corrections";
  auto created = SourceRateAverage::create(1000, 10, 100000LL);
  require(created.has_value(), context);
  auto subject = std::move(*created);
  context = "signed-corrections:0:add";
  require(subject.add(-500LL, 100000LL).has_value(), context);
  context = "signed-corrections:1:average";
  { const auto result = subject.average(101000LL);
    require(result && *result == -50LL, context); }
  context = "signed-corrections:2:add";
  require(subject.add(200LL, 101000LL).has_value(), context);
  context = "signed-corrections:3:sum";
  { const auto result = subject.sum(102000LL);
    require(result && *result == -300LL, context); }
}

void javaRateCase5() {
  std::string_view context = "slot-wrap";
  auto created = SourceRateAverage::create(1000, 10, 100000LL);
  require(created.has_value(), context);
  auto subject = std::move(*created);
  context = "slot-wrap:0:add";
  require(subject.add(9223372036854775807LL, 100000LL).has_value(), context);
  context = "slot-wrap:1:add";
  require(subject.add(9223372036854775807LL, 100000LL).has_value(), context);
  context = "slot-wrap:2:sum";
  { const auto result = subject.sum(101000LL);
    require(result && *result == -2LL, context); }
  context = "slot-wrap:3:average";
  { const auto result = subject.average(101000LL);
    require(result && *result == 0LL, context); }
}

void javaRateCase6() {
  std::string_view context = "sum-wrap";
  auto created = SourceRateAverage::create(1000, 10, 100000LL);
  require(created.has_value(), context);
  auto subject = std::move(*created);
  context = "sum-wrap:0:add";
  require(subject.add(9223372036854775807LL, 100000LL).has_value(), context);
  context = "sum-wrap:1:add";
  require(subject.add(9223372036854775807LL, 101000LL).has_value(), context);
  context = "sum-wrap:2:sum";
  { const auto result = subject.sum(102000LL);
    require(result && *result == -2LL, context); }
  context = "sum-wrap:3:average";
  { const auto result = subject.average(102000LL);
    require(result && *result == 0LL, context); }
}

void javaRateCase7() {
  std::string_view context = "minimum-long";
  auto created = SourceRateAverage::create(1000, 10, 100000LL);
  require(created.has_value(), context);
  auto subject = std::move(*created);
  context = "minimum-long:0:add";
  require(subject.add((-9223372036854775807LL - 1), 100000LL).has_value(), context);
  context = "minimum-long:1:sum";
  { const auto result = subject.sum(101000LL);
    require(result && *result == (-9223372036854775807LL - 1), context); }
  context = "minimum-long:2:average";
  { const auto result = subject.average(101000LL);
    require(result && *result == -922337203685477580LL, context); }
}

void javaRateCase8() {
  std::string_view context = "clear-and-explicit-clock-reset";
  auto created = SourceRateAverage::create(1000, 10, 100000LL);
  require(created.has_value(), context);
  auto subject = std::move(*created);
  context = "clear-and-explicit-clock-reset:0:add";
  require(subject.add(1234LL, 100000LL).has_value(), context);
  context = "clear-and-explicit-clock-reset:1:clear";
  require(subject.clear(50000LL).has_value(), context);
  context = "clear-and-explicit-clock-reset:2:average";
  { const auto result = subject.average(50000LL);
    require(result && *result == 0LL, context); }
  context = "clear-and-explicit-clock-reset:3:add";
  require(subject.add(700LL, 50000LL).has_value(), context);
  context = "clear-and-explicit-clock-reset:4:sum";
  { const auto result = subject.sum(51000LL);
    require(result && *result == 700LL, context); }
}

void javaRateCase9() {
  std::string_view context = "long-idle-and-ring-reuse";
  auto created = SourceRateAverage::create(1000, 10, 100000LL);
  require(created.has_value(), context);
  auto subject = std::move(*created);
  context = "long-idle-and-ring-reuse:0:add";
  require(subject.add(120LL, 100000LL).has_value(), context);
  context = "long-idle-and-ring-reuse:1:sum";
  { const auto result = subject.sum(999000LL);
    require(result && *result == 0LL, context); }
  context = "long-idle-and-ring-reuse:2:add";
  require(subject.add(15LL, 999000LL).has_value(), context);
  context = "long-idle-and-ring-reuse:3:sum";
  { const auto result = subject.sum(1000000LL);
    require(result && *result == 15LL, context); }
}

void javaRateCase10() {
  std::string_view context = "requested-period-clamping";
  auto created = SourceRateAverage::create(1000, 10, 100000LL);
  require(created.has_value(), context);
  auto subject = std::move(*created);
  context = "requested-period-clamping:0:add";
  require(subject.add(100LL, 100000LL).has_value(), context);
  context = "requested-period-clamping:1:add";
  require(subject.add(200LL, 101000LL).has_value(), context);
  context = "requested-period-clamping:2:add";
  require(subject.add(300LL, 102000LL).has_value(), context);
  context = "requested-period-clamping:3:period";
  { const auto result = subject.averageForMillis(-1, 103000LL);
    require(result && *result == 60LL, context); }
  context = "requested-period-clamping:4:period";
  { const auto result = subject.averageForMillis(1, 103000LL);
    require(result && *result == 300LL, context); }
  context = "requested-period-clamping:5:period";
  { const auto result = subject.averageForMillis(2000, 103000LL);
    require(result && *result == 250LL, context); }
  context = "requested-period-clamping:6:period";
  { const auto result = subject.averageForMillis(2147483647, 103000LL);
    require(result && *result == 60LL, context); }
}

void javaRateCase11() {
  std::string_view context = "two-second-refresh-point-unscaled";
  auto created = SourceRateAverage::create(2000, 10, 100000LL);
  require(created.has_value(), context);
  auto subject = std::move(*created);
  context = "two-second-refresh-point-unscaled:0:add";
  require(subject.add(1000LL, 100000LL).has_value(), context);
  context = "two-second-refresh-point-unscaled:1:point";
  { const auto result = subject.point(102000LL);
    require(result && *result == 1000LL, context); }
  context = "two-second-refresh-point-unscaled:2:period";
  { const auto result = subject.averageForMillis(1, 102000LL);
    require(result && *result == 1000LL, context); }
  context = "two-second-refresh-point-unscaled:3:period";
  { const auto result = subject.averageForMillis(4000, 102000LL);
    require(result && *result == 250LL, context); }
  context = "two-second-refresh-point-unscaled:4:average";
  { const auto result = subject.average(102000LL);
    require(result && *result == 100LL, context); }
}

void javaRateCase12() {
  std::string_view context = "full-capacity-window";
  auto created = SourceRateAverage::create(1000, 1800, 100000LL);
  require(created.has_value(), context);
  auto subject = std::move(*created);
  context = "full-capacity-window:0:add";
  require(subject.add(1800LL, 100000LL).has_value(), context);
  context = "full-capacity-window:1:average";
  { const auto result = subject.average(101000LL);
    require(result && *result == 1LL, context); }
  context = "full-capacity-window:2:average";
  { const auto result = subject.average(1900000LL);
    require(result && *result == 1LL, context); }
  context = "full-capacity-window:3:average";
  { const auto result = subject.average(1901000LL);
    require(result && *result == 0LL, context); }
}

void javaRateCase13() {
  std::string_view context = "maximum-admitted-time";
  auto created = SourceRateAverage::create(1000, 10, 9223372036854774000LL);
  require(created.has_value(), context);
  auto subject = std::move(*created);
  context = "maximum-admitted-time:0:add";
  require(subject.add(9LL, 9223372036854774000LL).has_value(), context);
  context = "maximum-admitted-time:1:sum";
  { const auto result = subject.sum(9223372036854775807LL);
    require(result && *result == 9LL, context); }
  context = "maximum-admitted-time:2:point";
  { const auto result = subject.point(9223372036854775807LL);
    require(result && *result == 9LL, context); }
}

void javaRateCase14() {
  std::string_view context = "default-thirty-and-ten-second-paths";
  GlobalFixture subject(30, 100000LL);
  context = "default-thirty-and-ten-second-paths:0:receive";
  subject.receive(3000LL, 100LL, false, 100000LL, context);
  context = "default-thirty-and-ten-second-paths:1:send";
  subject.send(1200LL, 60LL, false, 100000LL, context);
  context = "default-thirty-and-ten-second-paths:2:tick";
  require(subject.smoothing.tick(1, subject.configured, subject.totals, 100000LL).has_value(), context);
  context = "default-thirty-and-ten-second-paths:3:read";
  require(subject.read(101000LL, context) == GlobalObservation{SourceSmoothingPolicy{30,1}, {3000LL, 100LL, 1200LL, 60LL}, {300LL, 10LL, 120LL, 6LL}, {300LL, 10LL, 120LL, 6LL}, {310LL, 126LL}, {310LL, 126LL}, {103LL, 42LL}}, context);
  context = "default-thirty-and-ten-second-paths:4:read";
  require(subject.read(111000LL, context) == GlobalObservation{SourceSmoothingPolicy{30,1}, {3000LL, 100LL, 1200LL, 60LL}, {0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, {0LL, 0LL}, {0LL, 0LL}, {103LL, 42LL}}, context);
  context = "default-thirty-and-ten-second-paths:5:read";
  require(subject.read(131000LL, context) == GlobalObservation{SourceSmoothingPolicy{30,1}, {3000LL, 100LL, 1200LL, 60LL}, {0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, {0LL, 0LL}, {0LL, 0LL}, {0LL, 0LL}}, context);
}

void javaRateCase15() {
  std::string_view context = "lan-separation";
  GlobalFixture subject(30, 100000LL);
  context = "lan-separation:0:receive";
  subject.receive(1000LL, 100LL, true, 100000LL, context);
  context = "lan-separation:1:receive";
  subject.receive(500LL, 50LL, false, 100000LL, context);
  context = "lan-separation:2:send";
  subject.send(200LL, 20LL, true, 100000LL, context);
  context = "lan-separation:3:tick";
  require(subject.smoothing.tick(1, subject.configured, subject.totals, 100000LL).has_value(), context);
  context = "lan-separation:4:read";
  require(subject.read(101000LL, context) == GlobalObservation{SourceSmoothingPolicy{30,1}, {1500LL, 150LL, 200LL, 20LL}, {150LL, 15LL, 20LL, 2LL}, {50LL, 5LL, 0LL, 0LL}, {165LL, 22LL}, {165LL, 22LL}, {55LL, 7LL}}, context);
}

void javaRateCase16() {
  std::string_view context = "signed-cumulative-corrections";
  GlobalFixture subject(10, 100000LL);
  context = "signed-cumulative-corrections:0:receive";
  subject.receive(1000LL, 200LL, false, 100000LL, context);
  context = "signed-cumulative-corrections:1:tick";
  require(subject.smoothing.tick(1, subject.configured, subject.totals, 100000LL).has_value(), context);
  context = "signed-cumulative-corrections:2:receive";
  subject.receive(-300LL, -50LL, false, 101000LL, context);
  context = "signed-cumulative-corrections:3:tick";
  require(subject.smoothing.tick(2, subject.configured, subject.totals, 101000LL).has_value(), context);
  context = "signed-cumulative-corrections:4:read";
  require(subject.read(102000LL, context) == GlobalObservation{SourceSmoothingPolicy{10,1}, {700LL, 150LL, 0LL, 0LL}, {70LL, 15LL, 0LL, 0LL}, {70LL, 15LL, 0LL, 0LL}, {85LL, 0LL}, {85LL, 0LL}, {85LL, 0LL}}, context);
}

void javaRateCase17() {
  std::string_view context = "low-window-clamp";
  GlobalFixture subject(-2147483648, 100000LL);
  context = "low-window-clamp:0:receive";
  subject.receive(100LL, 10LL, false, 100000LL, context);
  context = "low-window-clamp:1:tick";
  require(subject.smoothing.tick(1, subject.configured, subject.totals, 100000LL).has_value(), context);
  context = "low-window-clamp:2:read";
  require(subject.read(101000LL, context) == GlobalObservation{SourceSmoothingPolicy{10,1}, {100LL, 10LL, 0LL, 0LL}, {10LL, 1LL, 0LL, 0LL}, {10LL, 1LL, 0LL, 0LL}, {11LL, 0LL}, {11LL, 0LL}, {11LL, 0LL}}, context);
}

void javaRateCase18() {
  std::string_view context = "high-window-clamp";
  GlobalFixture subject(2147483647, 100000LL);
  context = "high-window-clamp:0:receive";
  subject.receive(100LL, 10LL, false, 100000LL, context);
  context = "high-window-clamp:1:tick";
  require(subject.smoothing.tick(19, subject.configured, subject.totals, 100000LL).has_value(), context);
  context = "high-window-clamp:2:read";
  require(subject.read(101000LL, context) == GlobalObservation{SourceSmoothingPolicy{1800,20}, {100LL, 10LL, 0LL, 0LL}, {10LL, 1LL, 0LL, 0LL}, {10LL, 1LL, 0LL, 0LL}, {11LL, 0LL}, {11LL, 0LL}, {0LL, 0LL}}, context);
  context = "high-window-clamp:3:tick";
  require(subject.smoothing.tick(20, subject.configured, subject.totals, 120000LL).has_value(), context);
  context = "high-window-clamp:4:read";
  require(subject.read(140000LL, context) == GlobalObservation{SourceSmoothingPolicy{1800,20}, {100LL, 10LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, {0LL, 0LL}, {0LL, 0LL}, {0LL, 0LL}}, context);
}

void javaRateCase19() {
  std::string_view context = "reconfigure-tests-old-interval-first";
  GlobalFixture subject(120, 100000LL);
  context = "reconfigure-tests-old-interval-first:0:receive";
  subject.receive(1200LL, 100LL, false, 100000LL, context);
  context = "reconfigure-tests-old-interval-first:1:tick";
  require(subject.smoothing.tick(2, subject.configured, subject.totals, 100000LL).has_value(), context);
  context = "reconfigure-tests-old-interval-first:2:config";
  subject.configured = 30;
  context = "reconfigure-tests-old-interval-first:3:receive";
  subject.receive(600LL, 50LL, false, 101000LL, context);
  context = "reconfigure-tests-old-interval-first:4:tick";
  require(subject.smoothing.tick(3, subject.configured, subject.totals, 101000LL).has_value(), context);
  context = "reconfigure-tests-old-interval-first:5:read";
  require(subject.read(102000LL, context) == GlobalObservation{SourceSmoothingPolicy{30,1}, {1800LL, 150LL, 0LL, 0LL}, {180LL, 15LL, 0LL, 0LL}, {180LL, 15LL, 0LL, 0LL}, {195LL, 0LL}, {195LL, 0LL}, {10LL, 0LL}}, context);
  context = "reconfigure-tests-old-interval-first:6:tick";
  require(subject.smoothing.tick(4, subject.configured, subject.totals, 102000LL).has_value(), context);
  context = "reconfigure-tests-old-interval-first:7:read";
  require(subject.read(103000LL, context) == GlobalObservation{SourceSmoothingPolicy{30,1}, {1800LL, 150LL, 0LL, 0LL}, {180LL, 15LL, 0LL, 0LL}, {180LL, 15LL, 0LL, 0LL}, {195LL, 0LL}, {195LL, 0LL}, {21LL, 0LL}}, context);
}

void javaRateCase20() {
  std::string_view context = "interval-boundary-and-retained-baseline";
  GlobalFixture subject(119, 100000LL);
  context = "interval-boundary-and-retained-baseline:0:receive";
  subject.receive(1000LL, 100LL, false, 100000LL, context);
  context = "interval-boundary-and-retained-baseline:1:tick";
  require(subject.smoothing.tick(1, subject.configured, subject.totals, 100000LL).has_value(), context);
  context = "interval-boundary-and-retained-baseline:2:config";
  subject.configured = 120;
  context = "interval-boundary-and-retained-baseline:3:receive";
  subject.receive(500LL, 50LL, false, 101000LL, context);
  context = "interval-boundary-and-retained-baseline:4:tick";
  require(subject.smoothing.tick(2, subject.configured, subject.totals, 101000LL).has_value(), context);
  context = "interval-boundary-and-retained-baseline:5:read";
  require(subject.read(104000LL, context) == GlobalObservation{SourceSmoothingPolicy{120,2}, {1500LL, 150LL, 0LL, 0LL}, {150LL, 15LL, 0LL, 0LL}, {150LL, 15LL, 0LL, 0LL}, {165LL, 0LL}, {165LL, 0LL}, {4LL, 0LL}}, context);
}

void javaRateCase21() {
  std::string_view context = "counter-wrap-and-int-narrowing";
  GlobalFixture subject(10, 100000LL);
  context = "counter-wrap-and-int-narrowing:0:receive";
  subject.receive(9223372036854775807LL, 21474836480LL, false, 100000LL, context);
  context = "counter-wrap-and-int-narrowing:1:send";
  subject.send(9223372036854775807LL, 1LL, false, 100000LL, context);
  context = "counter-wrap-and-int-narrowing:2:tick";
  require(subject.smoothing.tick(1, subject.configured, subject.totals, 100000LL).has_value(), context);
  context = "counter-wrap-and-int-narrowing:3:receive";
  subject.receive(9223372036854775807LL, -1LL, false, 101000LL, context);
  context = "counter-wrap-and-int-narrowing:4:tick";
  require(subject.smoothing.tick(2, subject.configured, subject.totals, 101000LL).has_value(), context);
  context = "counter-wrap-and-int-narrowing:5:read";
  require(subject.read(102000LL, context) == GlobalObservation{SourceSmoothingPolicy{10,1}, {-2LL, 21474836479LL, 9223372036854775807LL, 1LL}, {0LL, 2147483647LL, 922337203685477580LL, 0LL}, {0LL, 2147483647LL, 922337203685477580LL, 0LL}, {2147483647LL, -858993460LL}, {2147483647LL, 922337203685477580LL}, {2147483647LL, -922337203685477580LL}}, context);
}

void javaRateCase22() {
  std::string_view context = "irregular-ticks-and-late-time";
  GlobalFixture subject(30, 100000LL);
  context = "irregular-ticks-and-late-time:0:receive";
  subject.receive(900LL, 0LL, false, 100000LL, context);
  context = "irregular-ticks-and-late-time:1:tick";
  require(subject.smoothing.tick(7, subject.configured, subject.totals, 100000LL).has_value(), context);
  context = "irregular-ticks-and-late-time:2:receive";
  subject.receive(600LL, 0LL, false, 107500LL, context);
  context = "irregular-ticks-and-late-time:3:tick";
  require(subject.smoothing.tick(11, subject.configured, subject.totals, 107500LL).has_value(), context);
  context = "irregular-ticks-and-late-time:4:read";
  require(subject.read(108000LL, context) == GlobalObservation{SourceSmoothingPolicy{30,1}, {1500LL, 0LL, 0LL, 0LL}, {150LL, 0LL, 0LL, 0LL}, {150LL, 0LL, 0LL, 0LL}, {150LL, 0LL}, {150LL, 0LL}, {50LL, 0LL}}, context);
  context = "irregular-ticks-and-late-time:5:read";
  require(subject.read(160000LL, context) == GlobalObservation{SourceSmoothingPolicy{30,1}, {1500LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL}, {0LL, 0LL}, {0LL, 0LL}, {0LL, 0LL}}, context);
}

void originalJavaRateCases() {
  javaRateCase1();
  javaRateCase2();
  javaRateCase3();
  javaRateCase4();
  javaRateCase5();
  javaRateCase6();
  javaRateCase7();
  javaRateCase8();
  javaRateCase9();
  javaRateCase10();
  javaRateCase11();
  javaRateCase12();
  javaRateCase13();
  javaRateCase14();
  javaRateCase15();
  javaRateCase16();
  javaRateCase17();
  javaRateCase18();
  javaRateCase19();
  javaRateCase20();
  javaRateCase21();
  javaRateCase22();
}
