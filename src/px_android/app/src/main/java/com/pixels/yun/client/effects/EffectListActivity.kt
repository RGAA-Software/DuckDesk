package com.pixels.yun.client.effects

import android.content.Intent
import android.content.res.Configuration
import android.os.Bundle
import android.text.TextUtils
import android.view.View
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.pixels.yun.client.R
import com.pixels.yun.client.databinding.FragmentEffectBinding
import com.pixels.yun.client.db.DBServer
import com.pixels.yun.client.ui.base.CustomAlertDialog
import com.pixels.yun.client.ui.base.OnListItemListener
import com.pixels.yun.client.ui.effects.EffectDisplayAdapter
import com.pixels.yun.client.ui.effects.EffectDisplayItemDecoration
import com.pixels.yun.client.ui.effects.EffectDisplayItemDecorationHorizontal

class EffectListActivity : AppCompatActivity() {

    private lateinit var effectDisplayAdapter: EffectDisplayAdapter
    private val effects: MutableList<EffectDefinition.EffectInfo> = mutableListOf()
    private val effectDefinition = EffectDefinition()
    private var serverEntity: DBServer? = null
    private lateinit var effectList: RecyclerView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_effect_list)
        serverEntity = intent.getSerializableExtra("serverEntity") as DBServer
        effectDefinition.init()
        effects.addAll(effectDefinition.effects)

        findViewById<TextView>(R.id.id_title_bar_text).text = getText(R.string.spectrums)
        findViewById<ImageView>(R.id.id_back).setOnClickListener {
            finish()
        }

        effectList = findViewById(R.id.effect_list)
        effectList.apply {
            var itemCount = 0
            if (this.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT) {
                itemCount = 2
                addItemDecoration(EffectDisplayItemDecoration());
            } else {
                itemCount = 4
                addItemDecoration(EffectDisplayItemDecorationHorizontal(4));
            }
            layoutManager = GridLayoutManager(this@EffectListActivity, itemCount)
            effectDisplayAdapter = EffectDisplayAdapter(context, effects);
            adapter = effectDisplayAdapter;
            effectDisplayAdapter.setOnItemClickListener(object:
                OnListItemListener<EffectDefinition.EffectInfo> {
                override fun onItemClicked(pos: Int, value: EffectDefinition.EffectInfo) {
                    val dialog = CustomAlertDialog.createDialog(this@EffectListActivity,
                        context.getString(R.string.open_spectrum_activity),
                        context.getString(R.string.do_you_want_to_open_spectrum_activity))
                    dialog.onSureClicked = View.OnClickListener {
                        startEffectActivity(value);
                    }
                    dialog.show()
                }
            })

            addOnScrollListener(object: RecyclerView.OnScrollListener() {
                override fun onScrolled(recyclerView: RecyclerView, dx: Int, dy: Int) {
                    super.onScrolled(recyclerView, dx, dy)
                }

                override fun onScrollStateChanged(recyclerView: RecyclerView, newState: Int) {
                    super.onScrollStateChanged(recyclerView, newState)
                    val manager = recyclerView.layoutManager as GridLayoutManager;
                    if (newState == RecyclerView.SCROLL_STATE_IDLE) {
                        val lastVisibleItem = manager.findLastCompletelyVisibleItemPosition();
                        if (lastVisibleItem == (effects.size - 1)) {
                            //Toast.makeText(activity, "Last...", Toast.LENGTH_SHORT).show();
                        }
                    }
                }
            });
        }
    }

    private fun startEffectActivity(value: EffectDefinition.EffectInfo) {
        if (serverEntity == null) {
            return
        }
        val intent = Intent(this, EffectActivity::class.java);
        val server = serverEntity!!
        if (!server.available || TextUtils.isEmpty(server.serverIp)) {
            Toast.makeText(this, "Server has not connected", Toast.LENGTH_SHORT).show()
            return;
        }
        intent.putExtra("ip", server.serverIp);
        intent.putExtra("port", server.streamWsPort);
        intent.putExtra("idx", value.idx)
        intent.putExtra("streamId", server.streamId)
        intent.putExtra("remoteDeviceId", if (server.serverId == null) {""} else {server.serverId})
        this.startActivity(intent)
    }
}