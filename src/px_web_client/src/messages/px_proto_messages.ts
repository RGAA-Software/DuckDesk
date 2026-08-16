import { Root, Type } from 'protobufjs'

export const PxProtoMsg = {
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
  PxProtoMsg.MsgRoot = root;

  // MessageType
  PxProtoMsg.MessageType = root.lookupEnum("px.MessageType");

  // VideoType
  PxProtoMsg.VideoType = root.lookupEnum("px.VideoType");

  PxProtoMsg.Message = root.lookupType("px.Message");
  PxProtoMsg.MsgHello = root.lookupType('px.Hello');

}