import { Root, Type } from 'protobufjs'

export const ProtoMessage = {
  Message: Type,
  MsgHello: Type,
}

export function loadMessageType(root: Root) {
  ProtoMessage.Message = root.lookupType("tc.Message")
  ProtoMessage.MsgHello = root.lookupType('tc.Hello')
}
