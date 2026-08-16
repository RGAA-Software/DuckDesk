package com.pixels.yun.client.ui.me

import android.os.Bundle
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.ui.text.font.FontVariation
import com.pixels.yun.client.BuildConfig
import com.pixels.yun.client.Settings
import com.pixels.yun.client.databinding.FragmentSettingsBinding
import com.pixels.yun.client.ui.BaseFragment

class SettingsFragment() : BaseFragment() {

    private lateinit var binding: FragmentSettingsBinding;

    companion object {
        const val TAG = "Main"
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View? {
        binding = FragmentSettingsBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        binding.idShowVirtualGamepad.isChecked = Settings.getInstance().isShowVirtualGamepad(requireActivity())
        binding.idShowVirtualGamepad.setOnCheckedChangeListener { buttonView, isChecked ->
            context?.let { Settings.getInstance().setShowVirtualGamepad(it, isChecked) }
        }

        binding.idInvertJoystickY.isChecked = Settings.getInstance().isInvertJoystickYAxis(requireActivity())
        binding.idInvertJoystickY.setOnCheckedChangeListener { buttonView, isChecked ->
            context?.let { Settings.getInstance().setInvertJoystickYAxis(it, isChecked) }
        }

        binding.idShowCursor.isChecked = Settings.getInstance().isShowCursor(requireActivity())
        binding.idShowCursor.setOnCheckedChangeListener { buttonView, isChecked ->
            context?.let { Settings.getInstance().setShowCursor(it, isChecked) }
        }

        binding.idFullscreen.isChecked = Settings.getInstance().isFullscreen(requireActivity())
        binding.idFullscreen.setOnCheckedChangeListener { buttView, isChecked ->
            context?.let { Settings.getInstance().setFullscreen(it, isChecked) }
        }

        // show monitor indicator
        binding.idMonitorIndicator.isChecked = Settings.getInstance().isShowMonitorIndicator(requireActivity())
        binding.idMonitorIndicator.setOnCheckedChangeListener { buttonView, isChecked ->
            requireActivity().let {
                Settings.getInstance().setShowMonitorIndicator(it, isChecked)
            }
        }

        // show logo
        binding.idShowLogo.isChecked = Settings.getInstance().isShowLogo(requireActivity())
        binding.idShowLogo.setOnCheckedChangeListener { buttonView, isChecked ->
            requireActivity().let {
                Settings.getInstance().setShowLogo(it, isChecked)
            }
        }

        binding.idVersion.setText("V " + BuildConfig.VERSION_NAME)

    }

}