// Generated from actual original Java stdout; do not author expected writer bytes here.
constexpr std::array<Expected, 1> Case0Step1Expected{{
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\n"},
}};
constexpr std::array<Expected, 2> Case0Step2Expected{{
  {"2023/12/31.dat", true, "s,1,28401119,101,202,303,404,505,606\n"},
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\n1,2,3,4,5,6\n"},
}};
constexpr std::array<Expected, 2> Case0Step3Expected{{
  {"2023/12/31.dat", true, "s,1,28401119,101,202,303,404,505,606\ne,1,28401119,1,2,3,4,5,6\n"},
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\n1,2,3,4,5,6\n"},
}};
constexpr std::array<Step, 3> Case0Steps{{
  {'S', 1704067200000LL, {10LL, 20LL, 30LL, 40LL, 50LL, 60LL}, {100LL, 200LL, 300LL, 400LL, 500LL, 600LL}, Case0Step1Expected},
  {'T', 1704067140000LL, {11LL, 22LL, 33LL, 44LL, 55LL, 66LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case0Step2Expected},
  {'E', 1704067140000LL, {12LL, 24LL, 36LL, 48LL, 60LL, 72LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case0Step3Expected},
}};
constexpr std::array<Expected, 1> Case1Step1Expected{{
  {"1970/01/01.dat", true, "s,1,0,100,200,300,400,500,600\n"},
}};
constexpr std::array<Expected, 1> Case1Step2Expected{{
  {"1970/01/01.dat", true, "s,1,0,100,200,300,400,500,600\n1,2,3,4,5,6\n"},
}};
constexpr std::array<Expected, 1> Case1Step3Expected{{
  {"1970/01/01.dat", true, "s,1,0,100,200,300,400,500,600\n1,2,3,4,5,6\ne,1,2,1,2,3,4,5,6\n"},
}};
constexpr std::array<Step, 3> Case1Steps{{
  {'S', 0LL, {10LL, 20LL, 30LL, 40LL, 50LL, 60LL}, {100LL, 200LL, 300LL, 400LL, 500LL, 600LL}, Case1Step1Expected},
  {'T', 60000LL, {11LL, 22LL, 33LL, 44LL, 55LL, 66LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case1Step2Expected},
  {'E', 120000LL, {12LL, 24LL, 36LL, 48LL, 60LL, 72LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case1Step3Expected},
}};
constexpr std::array<Expected, 1> Case2Step1Expected{{
  {"2024/01/01.dat", true, "s,1,28401120,0,0,0,0,0,0\n"},
}};
constexpr std::array<Step, 1> Case2Steps{{
  {'S', 1704067200000LL, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case2Step1Expected},
}};
constexpr std::array<Expected, 1> Case3Step1Expected{{
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\n"},
}};
constexpr std::array<Expected, 1> Case3Step2Expected{{
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\n1,2,3,4,5,6\n"},
}};
constexpr std::array<Expected, 1> Case3Step3Expected{{
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\n1,2,3,4,5,6\n1,2,3,4,5,6\n"},
}};
constexpr std::array<Expected, 1> Case3Step4Expected{{
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\n1,2,3,4,5,6\n1,2,3,4,5,6\ne,1,28401900,0,0,0,0,0,0\n"},
}};
constexpr std::array<Step, 4> Case3Steps{{
  {'S', 1704067200000LL, {10LL, 20LL, 30LL, 40LL, 50LL, 60LL}, {100LL, 200LL, 300LL, 400LL, 500LL, 600LL}, Case3Step1Expected},
  {'T', 1704114000000LL, {11LL, 22LL, 33LL, 44LL, 55LL, 66LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case3Step2Expected},
  {'T', 1704114000000LL, {12LL, 24LL, 36LL, 48LL, 60LL, 72LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case3Step3Expected},
  {'E', 1704114000000LL, {12LL, 24LL, 36LL, 48LL, 60LL, 72LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case3Step4Expected},
}};
constexpr std::array<Expected, 1> Case4Step1Expected{{
  {"2023/12/31.dat", true, "s,1,28401119,100,200,300,400,500,600\n"},
}};
constexpr std::array<Expected, 2> Case4Step2Expected{{
  {"2023/12/31.dat", true, "s,1,28401119,100,200,300,400,500,600\ne,1,28401120,1,2,3,4,5,6\n"},
  {"2024/01/01.dat", false, ""},
}};
constexpr std::array<Step, 2> Case4Steps{{
  {'S', 1704067140000LL, {10LL, 20LL, 30LL, 40LL, 50LL, 60LL}, {100LL, 200LL, 300LL, 400LL, 500LL, 600LL}, Case4Step1Expected},
  {'E', 1704067200000LL, {11LL, 22LL, 33LL, 44LL, 55LL, 66LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case4Step2Expected},
}};
constexpr std::array<Expected, 1> Case5Step1Expected{{
  {"2023/12/31.dat", true, "s,1,28401119,100,200,300,400,500,600\n"},
}};
constexpr std::array<Expected, 2> Case5Step2Expected{{
  {"2023/12/31.dat", true, "s,1,28401119,100,200,300,400,500,600\n1,2,3,4,5,6\n"},
  {"2024/01/01.dat", true, "s,1,28401120,101,202,303,404,505,606\n"},
}};
constexpr std::array<Expected, 2> Case5Step3Expected{{
  {"2023/12/31.dat", true, "s,1,28401119,100,200,300,400,500,600\n1,2,3,4,5,6\n"},
  {"2024/01/01.dat", true, "s,1,28401120,101,202,303,404,505,606\n1,2,3,4,5,6\n"},
}};
constexpr std::array<Expected, 2> Case5Step4Expected{{
  {"2023/12/31.dat", true, "s,1,28401119,100,200,300,400,500,600\n1,2,3,4,5,6\n"},
  {"2024/01/01.dat", true, "s,1,28401120,101,202,303,404,505,606\n1,2,3,4,5,6\ne,1,28401122,0,0,0,0,0,0\n"},
}};
constexpr std::array<Step, 4> Case5Steps{{
  {'S', 1704067140000LL, {10LL, 20LL, 30LL, 40LL, 50LL, 60LL}, {100LL, 200LL, 300LL, 400LL, 500LL, 600LL}, Case5Step1Expected},
  {'T', 1704067200000LL, {11LL, 22LL, 33LL, 44LL, 55LL, 66LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case5Step2Expected},
  {'T', 1704067260000LL, {12LL, 24LL, 36LL, 48LL, 60LL, 72LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case5Step3Expected},
  {'E', 1704067320000LL, {12LL, 24LL, 36LL, 48LL, 60LL, 72LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case5Step4Expected},
}};
constexpr std::array<Expected, 1> Case6Step1Expected{{
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\n"},
}};
constexpr std::array<Expected, 1> Case6Step2Expected{{
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\ne,1,28401121,1,2,3,4,5,6\n"},
}};
constexpr std::array<Expected, 1> Case6Step3Expected{{
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\ne,1,28401121,1,2,3,4,5,6\ns,1,28401122,101,202,303,404,505,606\n"},
}};
constexpr std::array<Expected, 1> Case6Step4Expected{{
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\ne,1,28401121,1,2,3,4,5,6\ns,1,28401122,101,202,303,404,505,606\ne,1,28401123,1,2,3,4,5,6\n"},
}};
constexpr std::array<Step, 4> Case6Steps{{
  {'S', 1704067200000LL, {10LL, 20LL, 30LL, 40LL, 50LL, 60LL}, {100LL, 200LL, 300LL, 400LL, 500LL, 600LL}, Case6Step1Expected},
  {'E', 1704067260000LL, {11LL, 22LL, 33LL, 44LL, 55LL, 66LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case6Step2Expected},
  {'S', 1704067320000LL, {11LL, 22LL, 33LL, 44LL, 55LL, 66LL}, {101LL, 202LL, 303LL, 404LL, 505LL, 606LL}, Case6Step3Expected},
  {'E', 1704067380000LL, {12LL, 24LL, 36LL, 48LL, 60LL, 72LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case6Step4Expected},
}};
constexpr std::array<Expected, 1> Case7Step1Expected{{
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\n"},
}};
constexpr std::array<Expected, 1> Case7Step2Expected{{
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\n-2,-3,-4,-5,-6,-7\n"},
}};
constexpr std::array<Expected, 1> Case7Step3Expected{{
  {"2024/01/01.dat", true, "s,1,28401120,100,200,300,400,500,600\n-2,-3,-4,-5,-6,-7\ne,1,28401122,1,1,1,1,1,1\n"},
}};
constexpr std::array<Step, 3> Case7Steps{{
  {'S', 1704067200000LL, {10LL, 20LL, 30LL, 40LL, 50LL, 60LL}, {100LL, 200LL, 300LL, 400LL, 500LL, 600LL}, Case7Step1Expected},
  {'T', 1704067260000LL, {8LL, 17LL, 26LL, 35LL, 44LL, 53LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case7Step2Expected},
  {'E', 1704067320000LL, {9LL, 18LL, 27LL, 36LL, 45LL, 54LL}, {0LL, 0LL, 0LL, 0LL, 0LL, 0LL}, Case7Step3Expected},
}};
constexpr std::array<Case, 8> Cases{{
  {"backward-date", Case0Steps},
  {"epoch-anchor", Case1Steps},
  {"header-only-zero", Case2Steps},
  {"late-and-duplicate", Case3Steps},
  {"midnight-end", Case4Steps},
  {"midnight-tick", Case5Steps},
  {"restart-same-file", Case6Steps},
  {"signed-adjustment", Case7Steps},
}};
