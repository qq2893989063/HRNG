package com.qualcomm.hrng

import android.os.Bundle
import android.widget.*
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

        setupUI()
        initHRNG()
    }

    private fun setupUI() {
        binding.btnGenerate.setOnClickListener {
            val minStr = binding.etMinValue.text.toString()
            val maxStr = binding.etMaxValue.text.toString()

            if (minStr.isEmpty() || maxStr.isEmpty()) {
                Toast.makeText(this, "Please enter min and max values", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            val minVal = minStr.toLongOrNull()
            val maxVal = maxStr.toLongOrNull()

            if (minVal == null || maxVal == null) {
                Toast.makeText(this, "Invalid number format", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            if (minVal > maxVal) {
                Toast.makeText(this, "Min cannot be greater than Max", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            lifecycleScope.launch {
                val result = withContext(Dispatchers.Default) {
                    nativeGenerateRandom(minVal, maxVal)
                }
                binding.tvResult.text = "Random: $result"
                appendHistory("[$minVal, $maxVal] -> $result")
            }
        }

        binding.btnBatchGenerate.setOnClickListener {
            val minStr = binding.etMinValue.text.toString()
            val maxStr = binding.etMaxValue.text.toString()
            val countStr = binding.etBatchCount.text.toString()

            if (minStr.isEmpty() || maxStr.isEmpty() || countStr.isEmpty()) {
                Toast.makeText(this, "Please fill all fields", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            val minVal = minStr.toLongOrNull() ?: run {
                Toast.makeText(this, "Invalid min value", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            val maxVal = maxStr.toLongOrNull() ?: run {
                Toast.makeText(this, "Invalid max value", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            val count = countStr.toIntOrNull() ?: run {
                Toast.makeText(this, "Invalid count", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            if (minVal > maxVal) {
                Toast.makeText(this, "Min cannot be greater than Max", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            if (count <= 0 || count > 10000) {
                Toast.makeText(this, "Count must be 1-10000", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            lifecycleScope.launch {
                binding.tvResult.text = "Generating..."

                val results = withContext(Dispatchers.Default) {
                    nativeGenerateBatch(minVal, maxVal, count)
                }

                val min = results.min()
                val max = results.max()
                val avg = results.average()

                val resultText = buildString {
                    appendLine("Batch: ${results.size} random numbers")
                    appendLine("Range: [$minVal, $maxVal]")
                    appendLine("-----------------")
                    appendLine("Min: $min")
                    appendLine("Max: $max")
                    appendLine("Avg: ${"%.2f".format(avg)}")
                    appendLine("-----------------")

                    val display = results.take(20)
                    appendLine("First ${display.size}:")
                    display.forEachIndexed { i, v ->
                        append("  [${i + 1}] $v  ")
                        if ((i + 1) % 4 == 0) appendLine()
                    }
                    if (results.size > 20) {
                        appendLine("  ... (${results.size - 20} more)")
                    }

                    appendLine("\nFrequency (last 1000):")
                    val sample = results.takeLast(1000.coerceAtMost(results.size))
                    val range = maxVal - minVal + 1
                    if (range <= 100) {
                        val freq = sample.groupingBy { it }.eachCount()
                        freq.toSortedMap().forEach { (v, c) ->
                            val pct = c.toDouble() / sample.size * 100
                            val bar = "#".repeat((pct / 2).toInt().coerceAtLeast(1))
                            appendLine("  $v: $c (${"%.1f".format(pct)}%) $bar")
                        }
                    } else {
                        appendLine("  Range too large for frequency analysis")
                    }
                }

                binding.tvResult.text = resultText
                appendHistory("Batch ${results.size}: [$minVal,$maxVal] min=$min max=$max avg=${"%.1f".format(avg)}")
            }
        }

        binding.btnRawEntropy.setOnClickListener {
            lifecycleScope.launch {
                val entropy = withContext(Dispatchers.Default) {
                    nativeReadRawEntropy(32)
                }
                binding.tvResult.text = entropy
                appendHistory("Raw entropy 32 bytes")
            }
        }
    }

    private fun initHRNG() {
        lifecycleScope.launch {
            val info = withContext(Dispatchers.Default) {
                nativeInitHRNG()
            }
            binding.tvDeviceInfo.text = info
            appendHistory("HRNG initialized")
        }
    }

    private fun appendHistory(entry: String) {
        val timestamp = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault())
            .format(java.util.Date())
        val current = binding.tvHistory.text.toString()
        binding.tvHistory.text = "[$timestamp] $entry\n$current"
    }
}