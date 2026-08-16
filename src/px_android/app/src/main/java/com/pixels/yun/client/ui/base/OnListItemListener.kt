package com.pixels.yun.client.ui.base

interface OnListItemListener<T> {
    fun onItemClicked(pos: Int, value: T);
}