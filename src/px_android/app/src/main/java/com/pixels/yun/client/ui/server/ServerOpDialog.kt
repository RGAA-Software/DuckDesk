package com.pixels.yun.client.ui.server

import android.app.AlertDialog
import android.content.Context
import android.content.res.Resources
import android.os.Bundle
import android.view.View
import com.pixels.yun.client.R

class ServerOpDialog(var ctx: Context) : AlertDialog(ctx) {

    lateinit var onShowGamesClicked: View.OnClickListener
    lateinit var onEffectListClicked: View.OnClickListener
    lateinit var onDeleteAppClicked: View.OnClickListener
    lateinit var onRestartServerClicked: View.OnClickListener
    lateinit var onAllProcessClicked: View.OnClickListener

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val view = View.inflate(context, R.layout.dialog_op_machine, null)
        setContentView(view)

        view.findViewById<View>(R.id.id_op_show_games).setOnClickListener {
            onShowGamesClicked.onClick(it)
            this.dismiss()
        }

        view.findViewById<View>(R.id.id_op_spectrum).setOnClickListener {
            onEffectListClicked.onClick(it)
            this.dismiss()
        }

        view.findViewById<View>(R.id.id_op_restart_server).setOnClickListener {
            onRestartServerClicked.onClick(it)
            this.dismiss()
        }

        view.findViewById<View>(R.id.id_op_all_processes).setOnClickListener {
            onAllProcessClicked.onClick(it)
            this.dismiss()
        }

        view.findViewById<View>(R.id.id_op_delete_server).setOnClickListener {
            onDeleteAppClicked.onClick(it)
            this.dismiss()
        }
    }

    override fun show() {
        super.show()

        window?.decorView?.layoutParams?.height?.let {
            window?.setLayout((Resources.getSystem().displayMetrics.density* 350).toInt(), it)
        }
    }

}