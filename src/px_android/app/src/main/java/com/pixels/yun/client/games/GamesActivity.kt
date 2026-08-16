package com.pixels.yun.client.games

import android.app.AlertDialog
import android.content.Intent
import android.content.res.Configuration
import android.os.Bundle
import android.os.Handler
import android.text.TextUtils
import android.util.Log
import android.view.View
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.simform.refresh.SSPullToRefreshLayout
import com.pixels.yun.client.App
import com.pixels.yun.client.AppContext
import com.pixels.yun.client.R
import com.pixels.yun.client.db.DBServer
import com.pixels.yun.client.events.OnRunningGames
import com.pixels.yun.client.events.OnServerAvailable
import com.pixels.yun.client.events.OnServerDeleted
import com.pixels.yun.client.events.OnServerEmpty
import com.pixels.yun.client.events.OnServerOffline
import com.pixels.yun.client.render.FrameRenderActivity
import com.pixels.yun.client.steam.SteamGame
import com.pixels.yun.client.ui.base.CustomAlertDialog
import com.pixels.yun.client.ui.steam.GameOpDialog
import com.pixels.yun.client.ui.steam.ItemDecoration
import com.pixels.yun.client.ui.steam.ItemDecorationHorizontal
import com.pixels.yun.client.ui.steam.SteamAppAdapter
import com.pixels.yun.client.ui.steam.SteamAppFragment
import org.greenrobot.eventbus.EventBus
import org.greenrobot.eventbus.Subscribe
import org.greenrobot.eventbus.ThreadMode

class GamesActivity : AppCompatActivity() {

    companion object {
        const val TAG = "Main";
    }

    private lateinit var steamAppAdapter: SteamAppAdapter
    private var steamGames = mutableListOf<SteamGame>();
    private var lastAvailableServer: DBServer? = null
    private lateinit var serverEntity: DBServer
    private lateinit var emptyIcon: View
    private lateinit var emptyTip: View
    private lateinit var refreshLayout: SSPullToRefreshLayout
    private lateinit var gameList: RecyclerView
    private lateinit var appContext: AppContext

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_games)
        EventBus.getDefault().register(this)
        appContext = (this.application as App).appContext
        serverEntity = intent.getSerializableExtra("serverEntity") as DBServer

        findViewById<TextView>(R.id.id_title_bar_text).text = getText(R.string.applications)
        findViewById<ImageView>(R.id.id_back).setOnClickListener {
            finish()
        }

        emptyIcon = findViewById<View>(R.id.id_empty_icon)
        emptyTip = findViewById<View>(R.id.id_empty_tip)
        emptyIcon.visibility = View.GONE
        emptyTip.visibility = View.GONE

        refreshLayout = findViewById(R.id.refresh_layout)
        gameList = findViewById(R.id.game_list)

        refreshLayout.apply {
            setRepeatMode(SSPullToRefreshLayout.RepeatMode.REPEAT);
            setRepeatCount(SSPullToRefreshLayout.RepeatCount.INFINITE);
            setRefreshStyle(SSPullToRefreshLayout.RefreshStyle.NORMAL);
            setLottieAnimation("lottie_clock.json");
            setOnRefreshListener {
                handler.postDelayed({
                    setRefreshing(false);
                }, 2000)
            }
        }

        gameList.apply {
            val itemCount: Int
            if (this.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT) {
                itemCount = 2
                addItemDecoration(ItemDecoration());
            } else {
                itemCount = 5
                addItemDecoration(ItemDecorationHorizontal(itemCount))
            }
            layoutManager = GridLayoutManager(this@GamesActivity, itemCount)
            steamAppAdapter = SteamAppAdapter(context, serverEntity!!, steamGames);
            steamAppAdapter.itemClickListener = object : SteamAppAdapter.OnItemClickListener {
                override fun onItemClicked(game: SteamGame) {
                    processGameClicked(game);
                }
            }

            adapter = steamAppAdapter;
            addOnScrollListener(object: RecyclerView.OnScrollListener() {
                override fun onScrolled(recyclerView: RecyclerView, dx: Int, dy: Int) {
                    super.onScrolled(recyclerView, dx, dy)
                }

                override fun onScrollStateChanged(recyclerView: RecyclerView, newState: Int) {
                    super.onScrollStateChanged(recyclerView, newState)
                    val manager = recyclerView.layoutManager as GridLayoutManager;
                    if (newState == RecyclerView.SCROLL_STATE_IDLE) {
                        val lastVisibleItem = manager.findLastCompletelyVisibleItemPosition();
                        if (lastVisibleItem == (steamGames.size - 1)) {
                            //Toast.makeText(this@GamesActivity, "Last...", Toast.LENGTH_SHORT).show();
                        }
                    }
                }
            });
        }
    }

    override fun onStart() {
        super.onStart()
    }

    override fun onResume() {
        super.onResume()
        requestSteamGames()
    }

    override fun onStop() {
        super.onStop()
    }

    override fun onDestroy() {
        super.onDestroy()
        EventBus.getDefault().unregister(this)
    }

//    override fun onRefresh() {
//        super.onRefresh()
//        requestSteamGames()
//    }

    private fun addPresetItems() {
        steamGames.add(SteamGame.create(1, "Desktop", SteamGame.TAG_PRESET));
        steamGames.add(SteamGame.create(2, "Steam Big Picture", SteamGame.TAG_PRESET));
    }

    @Subscribe(threadMode = ThreadMode.POSTING)
    fun onServerAvailableEvent(event: OnServerAvailable) {
        if (lastAvailableServer == null || lastAvailableServer?.serverId != event.server.serverId) {
            if (lastAvailableServer != null)
                Log.i(SteamAppFragment.TAG, "${lastAvailableServer!!.serverId} => ${event.server.serverId}")
            requestSteamGames();
        }
        lastAvailableServer = event.server
    }

    @Subscribe(threadMode = ThreadMode.POSTING)
    fun onServerOfflineEvent(event: OnServerOffline) {
        if (lastAvailableServer != null && event.server.serverId == lastAvailableServer!!.serverId) {
            clearApp()
        }
    }

    @Subscribe(threadMode = ThreadMode.MAIN)
    fun onServerEmptyEvent(event: OnServerEmpty) {
        clearApp()
    }

    @Subscribe(threadMode = ThreadMode.POSTING)
    fun onServerDeletedEvent(event: OnServerDeleted) {
        appContext.postDelayTask({
            requestSteamGames()
        }, 100)
    }

    @Subscribe(threadMode = ThreadMode.POSTING)
    fun onRunningGames(event: OnRunningGames) {
        appContext.postUITask {
            synchronized(SteamAppFragment::class.java) {
                val notifyIndices = mutableListOf<Int>()
                for (i in 0 until steamGames.size) {
                    val steamGame = steamGames[i]
                    var findInRunningGames = false;
                    event.runningGames.forEach { runningGame ->
                        if (steamGame.gameId == runningGame.gameId) {
                            if (steamGame.gameTag != SteamGame.TAG_RUNNING) {
                                steamGame.gameTag = SteamGame.TAG_RUNNING
                            }
                            findInRunningGames = true
                            notifyIndices.add(i)
                            return@forEach
                        }
                    }

                    if (!findInRunningGames) {
                        if (steamGame.gameTag == SteamGame.TAG_RUNNING) {
                            steamGame.gameTag = SteamGame.TAG_IDLE;
                            notifyIndices.add(i)
                        }
                    }
                }

                notifyIndices.forEach {
                    steamAppAdapter.notifyItemChanged(it, 0)
                    //Log.i(TAG, "Notify...$it")
                }
                //Log.i(TAG, "----------------------")
            }
        }
    }

    private fun clearApp() {
//        try {
//            Log.i(TAG, "ClearApp..." + Exception().stackTraceToString())
//        } catch (e: Exception){}
        appContext.postUITask {
            lastAvailableServer = null
            this.runOnUiThread {
                steamGames.clear()
                steamAppAdapter.notifyDataSetChanged()
                setEmptyVisibility(true)
            }
        }
    }

    private fun requestSteamGames() {
        appContext.postTask {
            if (serverEntity == null) {
                return@postTask
            }
            val result = appContext.steamManager.requestSteamGames(serverEntity!!)
            if (!result.ok()) {
                Log.i(SteamAppFragment.TAG, "requestSteamApps failed.");
                clearApp();
                return@postTask
            }
            if (steamGames.isEmpty()) {
                addPresetItems();
            }

            synchronized(SteamAppFragment::class.java) {
                steamGames.removeAll(result.value)
                steamGames.addAll(result.value)
                steamGames.sort()
            }

            appContext.postUITask{
                if (steamGames.isNotEmpty()) {
                    setEmptyVisibility(false)
                }
                steamAppAdapter.notifyDataSetChanged()
            }
        }
    }

    private fun requestRunningGames() {
        appContext.postTask {
            if (serverEntity == null) {
                return@postTask
            }
            val result = appContext.steamManager.requestRunningGames(serverEntity!!)
            if (!result.ok()) {
                Log.i(SteamAppFragment.TAG, "request running games failed");
                return@postTask
            }

            result.value.forEach {
                Log.i(SteamAppFragment.TAG, "running game: $it")
            }
        }
    }

    private fun setEmptyVisibility(visible: Boolean) {
        if (visible) {
            emptyIcon.visibility = View.VISIBLE
            emptyTip.visibility = View.VISIBLE
        } else {
            emptyIcon.visibility = View.GONE
            emptyTip.visibility = View.GONE
        }
    }

    fun processGameClicked(game: SteamGame) {
        val dialog = GameOpDialog(this)
        dialog.onStartGameClicked = View.OnClickListener {
            appContext.postTask {
                val gamePath = game.getGamePath()
                if (TextUtils.isEmpty(gamePath) && !game.isDesktop() && !game.isBigPictureMode()) {
                    AlertDialog.Builder(this)
                        .setTitle("Error")
                        .setMessage("Game exe path is empty")
                        .show()
                    return@postTask;
                }
                if (serverEntity == null) {
                    return@postTask
                }
                appContext.steamManager.startGame(serverEntity!!, gamePath)

                // after some conditions...
                // startFrameRenderActivity
                startFrameRenderActivity();
            }
        }

        dialog.onStopGameClicked = View.OnClickListener {
            val confirmDialog = CustomAlertDialog.createDialog(this,
                getString(R.string.stop_game),
                getString(R.string.do_you_want_to_stop_game));
            confirmDialog.onSureClicked = View.OnClickListener {
                appContext.postTask {
                    appContext.steamManager.stopGame(serverEntity!!, game.gameId.toString())
                }
            }
            confirmDialog.show()
        }
        dialog.show()
    }

    private fun startFrameRenderActivity() {
        if (serverEntity == null) {
            return
        }
        val intent = Intent(this, FrameRenderActivity::class.java);
        val server = serverEntity!!
        if (!server.available || TextUtils.isEmpty(server.serverIp)) {
            Toast.makeText(this, "Server has not connected", Toast.LENGTH_SHORT).show()
            return;
        }
        intent.putExtra("ip", server.serverIp)
        intent.putExtra("port", server.streamWsPort)
        intent.putExtra("streamId", server.streamId)
        intent.putExtra("remoteDeviceId", if (server.serverId == null) {""} else {server.serverId})
        this.startActivity(intent)
    }

}