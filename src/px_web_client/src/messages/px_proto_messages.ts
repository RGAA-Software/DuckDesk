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
  GrProtoMsg.MessageType = root.lookupEnum("px.MessageType");

  // VideoType
  GrProtoMsg.VideoType = root.lookupEnum("px.VideoType");

  GrProtoMsg.Message = root.lookupType("px.Message");
  GrProtoMsg.MsgHello = root.lookupType('px.Hello');

}