#include "KafkaOperator.h"
#include "Configer.h"
KafkaProducer::KafkaProducer()
{
    auto conf = Configer::getNode("server");
    auto broker = conf["kafka"]["broker"].as<std::string>();
    auto host = conf["kafka"]["host"].as<std::string>();
    auto port = conf["kafka"]["port"].as<std::string>();

    producerConf = std::shared_ptr<RdKafka::Conf>(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
    std::string ec;
    producerConf->set(broker, host + ":" + port, ec);

    producer = std::shared_ptr<RdKafka::Producer>(RdKafka::Producer::create(producerConf.get(), ec));
}

bool KafkaProducer::Produce(const std::string& topic, const std::string& data)
{
    RdKafka::ErrorCode resp = producer->produce(topic, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
                                                const_cast<char*>(data.c_str()), data.size(), nullptr, 0, 0, nullptr);
    if (resp != RdKafka::ERR_NO_ERROR) {
        spdlog::error("Produce failed: {}", RdKafka::err2str(resp));
        return false;
    }
    return true;
}
bool KafkaProducer::SaveMessage(const std::string& message) { return Produce("im-messages", message); }