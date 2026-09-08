package com.qualcomm.hrng

import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.qualcomm.hrng.databinding.ActivityMainBinding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    private external fun nativeInitHRNG(): String
    private external fun nativeGenerateRandom(minVal: Long, maxVal: Long): Long
    private external fun nativeGenerateBatch(minVal: Long, maxVal: Long, count: Int): LongArray
    private external fun nativeReadRawEntropy(byteCount: Int): String

    companion object {
        init {
            System.loadLibrary("qualcomm_hrng")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setSupportActionBar(binding.toolbar)
        setupUI()
        initHRNG()
    }

    private fun setupUI() {
        binding.btnGenerate.setOnClickListener {
            val minStr = binding.etMinValue.text.toString()
            val maxStr = binding.etMaxValue.text.toString()

            if (minStr.isEmpty() || maxStr.isEmpty()) {
                Toast.makeText(this, "请输入最小值和最大值", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            val minVal = minStr.toLongOrNull()
            val maxVal = maxStr.toLongOrNull()

            if (minVal == null || maxVal == null) {
                Toast.makeText(this, "无效的数字格式", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            if (minVal > maxVal) {
                Toast.makeText(this, "最小值不能大于最大值", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            lifecycleScope.launch {
                val result = withContext(Dispatchers.Default) {
                    nativeGenerateRandom(minVal, maxVal)
                }
                binding.tvResult.text = "随机数: $result"
                appendHistory("[$minVal, $maxVal] -> $result")
            }
        }

        binding.btnBatchGenerate.setOnClickListener {
            val minStr = binding.etMinValue.text.toString()
            val maxStr = binding.etMaxValue.text.toString()
            val countStr = binding.etBatchCount.text.toString()

            if (minStr.isEmpty() || maxStr.isEmpty() || countStr.isEmpty()) {
                Toast.makeText(this, "请填写所有字段", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            val minVal = minStr.toLongOrNull() ?: run {
                Toast.makeText(this, "无效的最小值", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            val maxVal = maxStr.toLongOrNull() ?: run {
                Toast.makeText(this, "无效的最大值", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            val count = countStr.toIntOrNull() ?: run {
                Toast.makeText(this, "无效的数量", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            if (minVal > maxVal) {
                Toast.makeText(this, "最小值不能大于最大值", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            if (count <= 0 || count > 10000) {
                Toast.makeText(this, "数量必须在 1-10000 之间", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            lifecycleScope.launch {
                binding.tvResult.text = "正在生成…"

                val results = withContext(Dispatchers.Default) {
                    nativeGenerateBatch(minVal, maxVal, count)
                }

                val min = results.min()
                val max = results.max()
                val avg = results.average()

                val resultText = buildString {
                    appendLine("批量: ${results.size} 个随机数")
                    appendLine("范围: [$minVal, $maxVal]")
                    appendLine("-----------------")
                    appendLine("最小值: $min")
                    appendLine("最大值: $max")
                    appendLine("平均值: ${"%.2f".format(avg)}")
                    appendLine("-----------------")

                    val display = results.take(20)
                    appendLine("前 ${display.size} 个:")
                    display.forEachIndexed { i, v ->
                        append("  [${i + 1}] $v  ")
                        if ((i + 1) % 4 == 0) appendLine()
                    }
                    if (results.size > 20) {
                        appendLine("  ... (还有 ${results.size - 20} 个)")
                    }

                    appendLine("\n频率分析 (最近 1000 个):")
                    val sample = results.takeLast(1000.coerceAtMost(results.size))
                    val range = maxVal - minVal + 1
                    if (range <= 100) {
                        val freq = sample.groupingBy { it }.eachCount()
                        freq.toSortedMap().forEach { (v, c) ->
                            val pct = c.toDouble() / sample.size * 100
                            val bar = "█".repeat((pct / 2).toInt().coerceAtLeast(1))
                            appendLine("  $v: $c (${ "%.1f".format(pct)}%) $bar")
                        }
                    } else {
                        appendLine("  范围过大，无法进行频率分析")
                    }
                }

                binding.tvResult.text = resultText
                appendHistory("批量 ${results.size}: [$minVal,$maxVal] min=$min max=$max avg=${ "%.1f".format(avg)}")
            }
        }

        binding.btnRawEntropy.setOnClickListener {
            lifecycleScope.launch {
                val entropy = withContext(Dispatchers.Default) {
                    nativeReadRawEntropy(32)
                }
                binding.tvResult.text = entropy
                appendHistory("原始熵 32 字节")
            }
        }
    }

    private fun initHRNG() {
        lifecycleScope.launch {
            val info = withContext(Dispatchers.Default) {
                nativeInitHRNG()
            }
            binding.tvDeviceInfo.text = info
            appendHistory("HRNG 已初始化")
        }
    }

    private fun appendHistory(entry: String) {
        val timestamp = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault())
            .format(java.util.Date())
        val current = binding.tvHistory.text.toString()
        binding.tvHistory.text = "[$timestamp] $entry\n$current"
    }
}
