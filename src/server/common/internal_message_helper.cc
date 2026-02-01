#include "common/internal_message_helper.h"

#include <flatbuffers/flatbuffers.h>

#include "internal_generated.h"

namespace mir2::common {

std::vector<uint8_t> BuildServiceHello(ServiceType service) {
  flatbuffers::FlatBufferBuilder builder;
  const auto hello = mir2::internal::CreateServiceHello(
      builder, static_cast<mir2::internal::ServiceType>(service));
  builder.Finish(hello);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildServiceHelloAck(ServiceType service, bool ok) {
  flatbuffers::FlatBufferBuilder builder;
  const auto ack = mir2::internal::CreateServiceHelloAck(
      builder, static_cast<mir2::internal::ServiceType>(service), ok);
  builder.Finish(ack);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildRoutedMessage(uint64_t client_id, uint16_t msg_id,
                                        const std::vector<uint8_t>& payload) {
  flatbuffers::FlatBufferBuilder builder;
  const auto payload_vec = builder.CreateVector(payload);
  const auto routed = mir2::internal::CreateRoutedMessage(builder, client_id, msg_id, payload_vec);
  builder.Finish(routed);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

bool ParseRoutedMessage(const std::vector<uint8_t>& buffer, RoutedMessageData* out_data) {
  if (!out_data || buffer.empty()) {
    return false;
  }

  flatbuffers::Verifier verifier(buffer.data(), buffer.size());
  if (!verifier.VerifyBuffer<mir2::internal::RoutedMessage>(nullptr)) {
    return false;
  }

  const auto* routed = flatbuffers::GetRoot<mir2::internal::RoutedMessage>(buffer.data());
  if (!routed) {
    return false;
  }

  out_data->client_id = routed->client_id();
  out_data->msg_id = routed->msg_id();
  out_data->payload.clear();

  if (routed->payload()) {
    out_data->payload.assign(routed->payload()->begin(), routed->payload()->end());
  }

  return true;
}

bool ParseServiceHello(const std::vector<uint8_t>& buffer, ServiceType* out_service) {
  if (!out_service || buffer.empty()) {
    return false;
  }

  flatbuffers::Verifier verifier(buffer.data(), buffer.size());
  if (!verifier.VerifyBuffer<mir2::internal::ServiceHello>(nullptr)) {
    return false;
  }

  const auto* hello = flatbuffers::GetRoot<mir2::internal::ServiceHello>(buffer.data());
  if (!hello) {
    return false;
  }

  *out_service = static_cast<ServiceType>(hello->service());
  return true;
}

bool ParseServiceHelloAck(const std::vector<uint8_t>& buffer, ServiceType* out_service, bool* out_ok) {
  if (!out_service || !out_ok || buffer.empty()) {
    return false;
  }

  flatbuffers::Verifier verifier(buffer.data(), buffer.size());
  if (!verifier.VerifyBuffer<mir2::internal::ServiceHelloAck>(nullptr)) {
    return false;
  }

  const auto* ack = flatbuffers::GetRoot<mir2::internal::ServiceHelloAck>(buffer.data());
  if (!ack) {
    return false;
  }

  *out_service = static_cast<ServiceType>(ack->service());
  *out_ok = ack->ok();
  return true;
}

}  // namespace mir2::common
