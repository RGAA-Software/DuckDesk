import { Root, Type } from 'protobufjs'

export const GrProtoMsg = {
  MsgRoot: Root,
  // MessageType
  MessageType: Type,
  // VideoType
  // kNetH264 = 0;
  // kNetHevc = 1;
  // kNetVp9 = 2;
  VideoType: Type,

  Message: Type,
  MsgHello: Type,
}

export function loadMessageType(root: Root) {
  GrProtoMsg.MsgRoot = root;

  // MessageType
  GrProtoMsg.MessageType = root.lookupEnum("tc.MessageType");

  // VideoType
  GrProtoMsg.VideoType = root.lookupEnum("tc.VideoType");

  GrProtoMsg.Message = root.lookupType("tc.Message");
  GrProtoMsg.MsgHello = root.lookupType('tc.Hello');

}