#include <random>
#include <iostream>
#include "gtest/gtest.h"
#include "trace_prot_generated.h"

class RandomNumberGenerator
{
  private:
    std::random_device r;
    std::default_random_engine e1;
    std::uniform_int_distribution<uint8_t> uniform_dist;
    std::seed_seq seed2;
    std::mt19937 e2;
    uint8_t mean;
    std::normal_distribution<> normal_dist;

  public:
    RandomNumberGenerator() :
      e1 {r()}, 
      uniform_dist {0, 255},
      seed2 {r(), r(), r(), r(), r(), r(), r(), r()},
      e2 {seed2},
      mean {uniform_dist(e1)},
      normal_dist {static_cast<double>(mean), 50}
    { 
      //mean=uniform_dist(e1);
      //normal_dist = std::normal_distribution<>(mean, 50);
    }

    uint8_t generate() {
      return static_cast<uint8_t>(normal_dist(e2));
    }

};


TEST(TraceSerializationTest, Serialize)
{
  std::vector<int> dims {4, 1024, 1024};
  auto elem_cnt = std::accumulate(std::begin(dims), std::end(dims), 1, std::multiplies<size_t>());

  std::vector<uint8_t> data(elem_cnt);

  RandomNumberGenerator rgen;
  std::function<uint8_t()> rnd = [&rgen] { return rgen.generate(); };

  std::generate(data.begin(), data.end(), rnd);
  //for(auto item : data) std::cout << "item=" << static_cast<unsigned>(item) << std::endl;
  //auto sum = std::accumulate(data.begin(), data.end(), 0, std::plus<uint64_t>());
  //std::cout << sum << std::endl;
  MONA::TraceDS::Dim3 ddims {dims[0], dims[1], dims[2]};

  int32_t seq = 25;
  float center = 1024.5;
  float rotation = 0.02;
  int32_t uniqueId = 32;

  std::vector<uint8_t> cdata(data);

  flatbuffers::FlatBufferBuilder fbuilder {1024};
  auto offsetImage = MONA::TraceDS::CreateTImageDirect(fbuilder,
                        seq,
                        &ddims,
                        rotation,
                        center,
                        uniqueId,
                        MONA::TraceDS::IType_End,
                        &data);
  fbuilder.Finish(offsetImage);
  //int size = fbuilder.GetSize();
  uint8_t *buf = fbuilder.GetBufferPointer();
 
  auto &timage = *(MONA::TraceDS::GetTImage(buf));

  ASSERT_EQ(seq, timage.seq());
  ASSERT_EQ(center, timage.center());
  ASSERT_EQ(rotation, timage.rotation());
  ASSERT_EQ(uniqueId, timage.uniqueId());

  for(int i=0; i<elem_cnt; ++i)
    ASSERT_EQ(data[i], (*timage.tdata())[i]);//->data()[i]);

  ASSERT_NE(0, timage.seq());
  ASSERT_NE(0, timage.center());
  ASSERT_NE(0, timage.rotation());
  ASSERT_NE(0, timage.uniqueId());
}

