#pragma once
#include "Const.h"
#include <librdkafka/rdkafkacpp.h>
#include <string>
#include <vector>

class KafkaProducer : public Singleton<KafkaProducer>
{
  public:
    KafkaProducer();
    ~KafkaProducer() = default;

    bool SaveMessage(const std::string& message);

  protected:
    bool Produce(const std::string& topic, const std::string& data);

  private:
    std::shared_ptr<RdKafka::Producer> producer;
    std::shared_ptr<RdKafka::Conf> producerConf;
    std::mutex producerMutex;
};
class KafkaConsumer : public Singleton<KafkaConsumer>
{
  public:
};