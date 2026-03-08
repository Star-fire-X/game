/**
 * @file typed_message_codec.h
 * @brief Strongly-typed FlatBuffers message binding helpers.
 */

#ifndef MIR2_COMMON_PROTOCOL_TYPED_MESSAGE_CODEC_H_
#define MIR2_COMMON_PROTOCOL_TYPED_MESSAGE_CODEC_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "common/protocol/packet_codec.h"

namespace mir2::common::protocol {

namespace detail {
template <typename T>
struct DependentFalse : std::false_type {};
}  // namespace detail

/**
 * @brief Typed binding for one protocol message.
 *
 * New protocol messages should define a concrete binding by inheriting this
 * template and overriding `FromFbs` + `ToFbs` for NativeType conversion.
 */
template <mir2::common::MsgId Id, typename FbsType, typename NativeType>
struct MessageBinding {
  using Fbs = FbsType;
  using Native = NativeType;
  static constexpr mir2::common::MsgId kMsgId = Id;

  static bool Validate(const uint8_t* payload, size_t payload_size) {
    if (!payload || payload_size == 0) {
      return false;
    }
    flatbuffers::Verifier verifier(payload, payload_size);
    return verifier.VerifyBuffer<FbsType>(nullptr);
  }

  static bool FromFbs(const FbsType&, NativeType*) { return false; }

  static flatbuffers::Offset<FbsType> ToFbs(flatbuffers::FlatBufferBuilder&,
                                            const NativeType&) {
    static_assert(detail::DependentFalse<FbsType>::value,
                  "MessageBinding::ToFbs must be specialized for this message.");
    return {};
  }
};

template <typename Binding>
bool ValidateTypedPayload(const uint8_t* payload, size_t payload_size) {
  return Binding::Validate(payload, payload_size);
}

template <typename Binding>
bool DecodeTypedPayload(const uint8_t* payload,
                        size_t payload_size,
                        typename Binding::Native* out_native) {
  if (!out_native || !Binding::Validate(payload, payload_size)) {
    return false;
  }

  const auto* root = flatbuffers::GetRoot<typename Binding::Fbs>(payload);
  if (!root) {
    return false;
  }
  return Binding::FromFbs(*root, out_native);
}

template <typename Binding>
std::optional<typename Binding::Native> DecodeTypedPayload(const uint8_t* payload,
                                                           size_t payload_size) {
  typename Binding::Native native{};
  if (!DecodeTypedPayload<Binding>(payload, payload_size, &native)) {
    return std::nullopt;
  }
  return native;
}

template <typename Binding>
std::vector<uint8_t> EncodeTypedPayload(const typename Binding::Native& native) {
  flatbuffers::FlatBufferBuilder builder;
  const auto root = Binding::ToFbs(builder, native);
  builder.Finish(root);

  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

template <typename Binding>
std::vector<uint8_t> EncodeTypedPacket(const typename Binding::Native& native) {
  const auto payload = EncodeTypedPayload<Binding>(native);
  return mir2::common::EncodePacketV2(
      static_cast<uint16_t>(Binding::kMsgId),
      payload.data(),
      payload.size(),
      /*sequence=*/0,
      /*flags=*/0);
}

}  // namespace mir2::common::protocol

#endif  // MIR2_COMMON_PROTOCOL_TYPED_MESSAGE_CODEC_H_
