package com.pixels.yun.client.ui.effects

import android.content.Context
import android.graphics.Typeface
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import androidx.recyclerview.widget.RecyclerView.ViewHolder
import com.bumptech.glide.Glide
import com.pixels.yun.client.R
import com.pixels.yun.client.effects.EffectDefinition
import com.pixels.yun.client.ui.base.OnListItemListener

class EffectDisplayAdapter(private var context: Context, private var apps: MutableList<EffectDefinition.EffectInfo>) :
    RecyclerView.Adapter<EffectDisplayAdapter.BookViewHolder>() {

    private val TAG = "Steam";

    private var itemClickListener: OnListItemListener<EffectDefinition.EffectInfo>? = null

    class BookViewHolder(itemView: View) : ViewHolder(itemView) {
        val cover: ImageView = itemView.findViewById(R.id.effect_cover)
        val appName: TextView = itemView.findViewById(R.id.id_app_name)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): BookViewHolder {
        val view = View.inflate(context, R.layout.item_effect, null)
        return BookViewHolder(view)
    }

    override fun getItemCount(): Int {
        return apps.size
    }

    override fun onBindViewHolder(holder: BookViewHolder, position: Int) {
        val app = apps[position];
        holder.itemView.setOnClickListener {
            itemClickListener?.onItemClicked(position, app)
        }

        holder.appName.text = app.name
        Glide.with(holder.cover).load(app.iconResId).into(holder.cover)
    }

    fun setOnItemClickListener(listener: OnListItemListener<EffectDefinition.EffectInfo>) {
        itemClickListener = listener
    }

}