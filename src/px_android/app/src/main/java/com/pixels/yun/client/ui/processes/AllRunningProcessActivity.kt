package com.pixels.yun.client.ui.processes

import android.graphics.Typeface
import android.os.Bundle
import android.text.TextUtils
import android.util.Log
import android.view.View
import android.view.View.SCROLLBARS_OUTSIDE_OVERLAY
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.ui.text.toLowerCase
import androidx.recyclerview.widget.LinearLayoutManager
import com.pixels.yun.client.App
import com.pixels.yun.client.AppContext
import com.pixels.yun.client.R
import com.pixels.yun.client.ServerApi
import com.pixels.yun.client.Settings
import com.pixels.yun.client.databinding.ActivityRunningProcessBinding
import com.pixels.yun.client.db.DBServer
import com.pixels.yun.client.util.HttpUtil
import com.pixels.yun.client.util.Result
import org.json.JSONObject
import java.util.Collections
import java.util.Locale

class AllRunningProcessActivity : AppCompatActivity() {

    companion object {
        const val TAG = "Main"
    }

    private lateinit var binding: ActivityRunningProcessBinding
    private lateinit var runningProcessAdapter: RunningProcessAdapter
    private var processes = mutableListOf<RunningProcess>()
    private lateinit var appContext: AppContext;
    private lateinit var serverEntity: DBServer

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityRunningProcessBinding.inflate(layoutInflater)
        setContentView(binding.root)
        appContext = (application as App).appContext;

        serverEntity = intent.getSerializableExtra("serverEntity") as DBServer

        val listview = binding.idRunningProcesses
        listview.layoutManager = LinearLayoutManager(this)
        listview.scrollBarStyle = SCROLLBARS_OUTSIDE_OVERLAY
        runningProcessAdapter = RunningProcessAdapter(this, serverEntity, processes)
        listview.adapter = runningProcessAdapter
        runningProcessAdapter.itemClickListener = object: RunningProcessAdapter.OnItemClickListener {
            override fun onItemClicked(game: RunningProcess) {
                AlertDialog.Builder(this@AllRunningProcessActivity)
                    .setTitle("Kill Process")
                    .setMessage("Do you want to kill: ${game.exePath} ?")
                    .setNegativeButton("Cancel") { _, _ ->

                    }
                    .setPositiveButton("Sure") { _, _ ->
                        killProcess(game.pid)
                    }
                    .show()
            }
        }

        binding.root.findViewById<TextView>(R.id.id_title_bar_text).text = "All Running Processes"
        binding.root.findViewById<ImageView>(R.id.id_back).setOnClickListener {
            finish()
        }
        val refreshView = binding.root.findViewById<ImageView>(R.id.id_refresh)
        refreshView.visibility = View.VISIBLE
        refreshView.setOnClickListener {
            loadRunningProcesses()
        }

        loadRunningProcesses()
    }

    private fun loadRunningProcesses() {
        appContext.postNetworkTask {
            val url =  serverEntity.apiBaseUrl + ServerApi.API_RUNNING_PROCESSES
            try {
                val resp = HttpUtil.reqUrl(url)
                val obj = JSONObject(resp);
                val code = obj.getInt("code");
                val msg = obj.getString("message");
                processes.clear()
                if (obj.has("data")) {
                    val data = obj.getJSONArray("data");
                    for (i in 0 until data.length()) {
                        val rp = RunningProcess();
                        val item = data.getJSONObject(i);
                        rp.pid = item.getInt("pid");
                        rp.exePath = item.getString("exe_path")
                        val splitItems = rp.exePath.split("/")
                        rp.exeName = splitItems[splitItems.size-1].lowercase(Locale.ROOT)
                        rp.iconName = item.getString("icon")
                        processes.add(rp)
                    }

                    processes.sortWith(compareBy {
                        it.exeName
                    })
                }

                appContext.postUITask {
                    runningProcessAdapter.notifyDataSetChanged()
                }
            } catch (e: Exception) {
                Log.e(TAG, "load running processes failed: ${e.message}")
                appContext.postUITask {
                    Toast.makeText(this, "Refresh process failed!", Toast.LENGTH_SHORT).show()
                    processes.clear()
                    runningProcessAdapter.notifyDataSetChanged()
                }
            }
        }
    }

    private fun killProcess(pid: Int) {
        appContext.postNetworkTask {
            val url = serverEntity.apiBaseUrl + ServerApi.API_KILL_PROCESS
            val params = mutableMapOf<String, String>()
            params["pid"] = pid.toString()
            val resp = HttpUtil.postUrl(url, params)
            Log.i(TAG, "resp: $resp")
            if (TextUtils.isEmpty(resp)) {
                return@postNetworkTask
            }
            appContext.postUITask {
                Toast.makeText(this, "Kill process success !", Toast.LENGTH_SHORT).show()
            }

            // refresh
            loadRunningProcesses()
        }
    }

}