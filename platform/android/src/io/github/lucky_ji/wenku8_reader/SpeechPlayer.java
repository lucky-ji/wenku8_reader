// SPDX-License-Identifier: GPL-2.0-only
package io.github.lucky_ji.wenku8_reader;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.speech.tts.Voice;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** System TTS adapter. It owns no network client, file cache, service or background notification. */
public final class SpeechPlayer
{
    private static final Map<Long, SpeechPlayer> m_oPlayers = new HashMap<>();
    private static final Handler m_oMain = new Handler(Looper.getMainLooper());
    private static final String m_strDefaultVoice = "系统默认中文 [网络情况未知]";
    private final Context m_oContext;
    private final long m_nOwner;
    private final AudioManager m_oAudio;
    private final Map<String, Voice> m_oVoices = new LinkedHashMap<>();
    private TextToSpeech m_oTts;
    private AudioFocusRequest m_oFocusRequest;
    private boolean m_bInitialized = false;
    private boolean m_bAvailable = false;
    private boolean m_bClosed = false;
    private boolean m_bNetwork = false;
    private boolean m_bHasFocus = false;
    private boolean m_bReceiverRegistered = false;
    private boolean m_bDefaultChinese = false;
    private String m_strVoice = "";
    private String m_strRequestedVoice = "";
    private String m_strError = "正在初始化系统语音引擎…";
    private String m_strUtterance = "";
    private long m_nUtterance = 0;
    private long m_nFocusEpoch = 0;
    private long m_nFocusGeneration = 0;
    private long m_nVoiceRevision = 0;
    private AudioManager.OnAudioFocusChangeListener m_oFocusListener;
    private float m_fRate = 1.0f;
    private float m_fPitch = 1.0f;
    private float m_fVolume = 1.0f;

    private static native void nativeEvent(long nOwner, int nEvent, long nToken, String strData);

    private final BroadcastReceiver m_oNoisyReceiver = new BroadcastReceiver()
    {
        @Override public void onReceive(Context oContext, Intent oIntent)
        {
            if (AudioManager.ACTION_AUDIO_BECOMING_NOISY.equals(oIntent.getAction()))
            {
                interrupt("耳机或音频设备已断开，朗读已暂停。");
            }
        }
    };

    private SpeechPlayer(Activity oActivity, long nOwner)
    {
        m_oContext = oActivity.getApplicationContext();
        m_nOwner = nOwner;
        m_oAudio = (AudioManager) m_oContext.getSystemService(Context.AUDIO_SERVICE);
        try
        {
            m_oTts = new TextToSpeech(m_oContext, nStatus -> m_oMain.post(() -> initialize(nStatus)));
        }
        catch (RuntimeException oIgnored)
        {
            m_strError = "无法启动系统语音引擎，请在系统文字转语音设置中检查引擎与语音数据。";
            publishReady();
        }
    }

    public static void create(Activity oActivity, long nOwner)
    {
        m_oMain.post(() ->
        {
            final SpeechPlayer oPrevious = m_oPlayers.remove(nOwner);
            if (oPrevious != null)
            {
                oPrevious.close();
            }
            if (oActivity == null)
            {
                nativeEvent(nOwner, 3, 0, "系统语音暂不可用，请返回阅读页后重试。");
                return;
            }
            m_oPlayers.put(nOwner, new SpeechPlayer(oActivity, nOwner));
        });
    }

    private interface PlayerAction
    {
        void run(SpeechPlayer oPlayer);
    }

    private static void withPlayer(long nOwner, PlayerAction fnAction)
    {
        m_oMain.post(() ->
        {
            final SpeechPlayer oPlayer = m_oPlayers.get(nOwner);
            if (oPlayer != null && !oPlayer.m_bClosed)
            {
                fnAction.run(oPlayer);
            }
        });
    }

    private static boolean chinese(Voice oVoice)
    {
        return oVoice != null && "zh".equals(oVoice.getLocale().getLanguage());
    }

    private static int priority(Voice oVoice)
    {
        return (chinese(oVoice) ? 0 : 2) + (needsNetwork(oVoice) ? 1 : 0);
    }

    private static boolean missingData(Voice oVoice)
    {
        return oVoice.getFeatures() != null &&
               oVoice.getFeatures().contains(TextToSpeech.Engine.KEY_FEATURE_NOT_INSTALLED);
    }

    private static boolean needsNetwork(Voice oVoice)
    {
        return oVoice.isNetworkConnectionRequired() || missingData(oVoice);
    }

    private void initialize(int nStatus)
    {
        if (m_bClosed)
        {
            return;
        }
        if (nStatus != TextToSpeech.SUCCESS || m_oTts == null)
        {
            m_strError = "系统语音引擎未就绪，请安装或启用文字转语音引擎及中文语音数据。";
            publishReady();
            return;
        }
        m_bInitialized = true;
        m_oTts.setOnUtteranceProgressListener(new UtteranceProgressListener()
        {
            @Override public void onStart(String strId)
            {
                m_oMain.post(() ->
                {
                    if (current(strId))
                    {
                        nativeEvent(m_nOwner, 1, m_nUtterance, "");
                    }
                });
            }
            @Override public void onDone(String strId)
            {
                m_oMain.post(() ->
                {
                    if (current(strId))
                    {
                        final long nToken = m_nUtterance;
                        m_strUtterance = "";
                        nativeEvent(m_nOwner, 2, nToken, "");
                        releaseFocusWhenIdle();
                    }
                });
            }
            @Override public void onError(String strId)
            {
                reportError(strId, "系统语音合成失败，请检查音色及语音数据后重试。");
            }
            @Override public void onError(String strId, int nError)
            {
                reportError(strId, "系统语音合成失败（代码 " + nError + "），请检查音色及语音数据。");
            }
        });
        try
        {
            final Set<Voice> oInstalled = m_oTts.getVoices();
            final ArrayList<Voice> vVoices = new ArrayList<>();
            if (oInstalled != null)
            {
                for (Voice oVoice : oInstalled)
                {
                    if (oVoice != null && oVoice.getLocale() != null)
                    {
                        vVoices.add(oVoice);
                    }
                }
            }
            Collections.sort(vVoices, new Comparator<Voice>()
            {
                @Override public int compare(Voice oLeft, Voice oRight)
                {
                    final int nPriority = Integer.compare(priority(oLeft), priority(oRight));
                    return nPriority != 0 ? nPriority : oLeft.getName().compareTo(oRight.getName());
                }
            });
            for (Voice oVoice : vVoices)
            {
                final String strLabel = oVoice.getName() + " · " +
                    oVoice.getLocale().getDisplayName(Locale.SIMPLIFIED_CHINESE) +
                    (oVoice.isNetworkConnectionRequired() ? " [需联网]"
                          : missingData(oVoice) ? " [需下载语音]" : " [离线]");
                m_oVoices.put(strLabel, oVoice);
            }
            if (!m_oVoices.isEmpty())
            {
                final String strRequested = resolveVoice(m_strRequestedVoice);
                selectVoice(!strRequested.isEmpty() ? strRequested : m_oVoices.keySet().iterator().next());
                return;
            }
            if (m_oTts.isLanguageAvailable(Locale.SIMPLIFIED_CHINESE) >= TextToSpeech.LANG_AVAILABLE &&
                m_oTts.setLanguage(Locale.SIMPLIFIED_CHINESE) >= TextToSpeech.LANG_AVAILABLE)
            {
                m_bDefaultChinese = true;
                m_bAvailable = true;
                m_bNetwork = true;
                m_strVoice = m_strDefaultVoice;
                m_strError = "系统未提供音色网络信息；使用默认中文前，请确认是否允许联网合成。";
            }
            else
            {
                m_strError = "未找到可用的中文语音，请在系统文字转语音设置中安装语音数据。";
            }
        }
        catch (RuntimeException oIgnored)
        {
            m_bAvailable = false;
            m_strError = "无法读取系统音色，请检查文字转语音引擎与语音数据。";
        }
        publishReady();
    }

    private boolean current(String strId)
    {
        return !m_bClosed && !m_strUtterance.isEmpty() && m_strUtterance.equals(strId);
    }

    private String resolveVoice(String strRequested)
    {
        if (strRequested == null || strRequested.isEmpty())
        {
            return "";
        }
        if (m_oVoices.containsKey(strRequested))
        {
            return strRequested;
        }
        // A downloaded voice can change from [需下载语音] to [离线]. Its engine
        // name remains stable, so do not lose the user's preference with the label.
        final int nSeparator = strRequested.indexOf(" · ");
        final String strId = nSeparator >= 0 ? strRequested.substring(0, nSeparator) : strRequested;
        for (Map.Entry<String, Voice> oEntry : m_oVoices.entrySet())
        {
            if (oEntry.getValue().getName().equals(strId))
            {
                return oEntry.getKey();
            }
        }
        return "";
    }

    private void publishReady()
    {
        if (m_bClosed)
        {
            return;
        }
        try
        {
            final JSONObject oData = new JSONObject();
            final JSONArray vNames = new JSONArray();
            for (String strName : m_oVoices.keySet())
            {
                vNames.put(strName);
            }
            if (m_bDefaultChinese)
            {
                vNames.put(m_strDefaultVoice);
            }
            oData.put("available", m_bAvailable);
            oData.put("network", m_bNetwork);
            oData.put("voiceRevision", m_nVoiceRevision);
            oData.put("voice", m_strVoice);
            oData.put("voices", vNames);
            oData.put("error", m_strError);
            nativeEvent(m_nOwner, 0, 0, oData.toString());
        }
        catch (JSONException oIgnored)
        {
            nativeEvent(m_nOwner, 3, 0, "系统语音状态读取失败。");
        }
    }

    private void selectVoice(String strName)
    {
        stopPlayback();
        if (!m_bInitialized || m_oTts == null)
        {
            return;
        }
        final String strResolved = resolveVoice(strName);
        final Voice oVoice = m_oVoices.get(strResolved);
        if (oVoice == null)
        {
            if (m_bDefaultChinese && m_strDefaultVoice.equals(strName))
            {
                m_bAvailable = true;
                m_bNetwork = true;
                m_strVoice = strName;
                publishReady();
            }
            else
            {
                m_strError = "所选音色已不可用，已保留当前音色；请重新选择。";
                publishReady();
            }
            return;
        }
        try
        {
            if (m_oTts.setVoice(oVoice) != TextToSpeech.SUCCESS)
            {
                m_bAvailable = false;
                m_strError = "这个音色暂不可用，请在系统设置中安装语音数据或改用其他音色。";
            }
            else
            {
                m_bAvailable = true;
                m_bNetwork = needsNetwork(oVoice);
                m_strVoice = strResolved;
                m_strError = chinese(oVoice) ? "" : "当前音色不是中文，建议选择中文音色朗读小说。";
            }
        }
        catch (RuntimeException oIgnored)
        {
            m_bAvailable = false;
            m_strError = "无法使用所选音色，请检查系统文字转语音设置。";
        }
        publishReady();
    }

    private void reportError(String strId, String strError)
    {
        m_oMain.post(() ->
        {
            if (current(strId))
            {
                final long nToken = m_nUtterance;
                stopPlayback();
                nativeEvent(m_nOwner, 3, nToken, strError);
            }
        });
    }

    private boolean requestFocus()
    {
        ++m_nFocusEpoch;
        if (m_bHasFocus)
        {
            return true;
        }
        if (m_oAudio == null)
        {
            return false;
        }
        final long nGeneration = ++m_nFocusGeneration;
        m_oFocusListener = nChange -> m_oMain.post(() ->
        {
            if (nGeneration == m_nFocusGeneration &&
                (nChange == AudioManager.AUDIOFOCUS_LOSS ||
                 nChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT ||
                 nChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK))
            {
                interrupt("音频焦点已被其他应用占用，朗读已暂停。");
            }
        });
        final int nResult;
        if (Build.VERSION.SDK_INT >= 26)
        {
            m_oFocusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                .setWillPauseWhenDucked(true).setOnAudioFocusChangeListener(m_oFocusListener, m_oMain).build();
            nResult = m_oAudio.requestAudioFocus(m_oFocusRequest);
        }
        else
        {
            nResult = m_oAudio.requestAudioFocus(m_oFocusListener, AudioManager.STREAM_MUSIC,
                                               AudioManager.AUDIOFOCUS_GAIN_TRANSIENT);
        }
        m_bHasFocus = nResult == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        return m_bHasFocus;
    }

    private void releaseFocus()
    {
        ++m_nFocusEpoch;
        ++m_nFocusGeneration;
        if (m_oAudio != null && m_bHasFocus)
        {
            try
            {
                if (Build.VERSION.SDK_INT >= 26 && m_oFocusRequest != null)
                {
                    m_oAudio.abandonAudioFocusRequest(m_oFocusRequest);
                }
                else
                {
                    m_oAudio.abandonAudioFocus(m_oFocusListener);
                }
            }
            catch (RuntimeException oIgnored)
            {
            }
        }
        m_bHasFocus = false;
        m_oFocusRequest = null;
        m_oFocusListener = null;
        if (m_bReceiverRegistered)
        {
            try
            {
                m_oContext.unregisterReceiver(m_oNoisyReceiver);
            }
            catch (IllegalArgumentException oIgnored)
            {
            }
            m_bReceiverRegistered = false;
        }
    }

    private void releaseFocusWhenIdle()
    {
        final long nEpoch = m_nFocusEpoch;
        m_oMain.postDelayed(() ->
        {
            if (!m_bClosed && m_strUtterance.isEmpty() && m_nFocusEpoch == nEpoch)
            {
                releaseFocus();
            }
        }, 350);
    }

    private void interrupt(String strReason)
    {
        if (m_bClosed || (!m_bHasFocus && m_strUtterance.isEmpty()))
        {
            return;
        }
        final long nToken = m_nUtterance;
        stopPlayback();
        nativeEvent(m_nOwner, 4, nToken, strReason);
    }

    private void stopPlayback()
    {
        m_strUtterance = "";
        if (m_oTts != null && m_bInitialized)
        {
            try
            {
                m_oTts.stop();
            }
            catch (RuntimeException oIgnored)
            {
            }
        }
        releaseFocus();
    }

    private void play(String strText, long nToken)
    {
        if (!m_bAvailable || m_oTts == null)
        {
            nativeEvent(m_nOwner, 3, nToken, m_strError);
            return;
        }
        if (strText == null || strText.trim().isEmpty() || strText.length() > TextToSpeech.getMaxSpeechInputLength())
        {
            nativeEvent(m_nOwner, 3, nToken, "这一段文字过长或为空，请换一段后重试。");
            return;
        }
        try
        {
            // Invalidate the old ID before QUEUE_FLUSH can deliver an old completion.
            m_strUtterance = "";
            m_oTts.stop();
            m_nUtterance = nToken;
            if (!requestFocus())
            {
                nativeEvent(m_nOwner, 4, nToken, "当前无法取得音频焦点，朗读已暂停。");
                return;
            }
            if (!m_bReceiverRegistered)
            {
                m_oContext.registerReceiver(m_oNoisyReceiver,
                    new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY));
                m_bReceiverRegistered = true;
            }
            m_strUtterance = Long.toString(nToken);
            m_oTts.setSpeechRate(m_fRate);
            m_oTts.setPitch(m_fPitch);
            final Bundle oParameters = new Bundle();
            oParameters.putFloat(TextToSpeech.Engine.KEY_PARAM_VOLUME, m_fVolume);
            final AudioAttributes oAttributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build();
            m_oTts.setAudioAttributes(oAttributes);
            if (m_oTts.speak(strText, TextToSpeech.QUEUE_FLUSH, oParameters, m_strUtterance) == TextToSpeech.ERROR)
            {
                final String strId = m_strUtterance;
                reportError(strId, "系统语音无法开始朗读，请检查语音引擎和音色。");
            }
        }
        catch (RuntimeException oIgnored)
        {
            stopPlayback();
            nativeEvent(m_nOwner, 3, nToken, "系统语音暂不可用，请在系统文字转语音设置中检查后重试。");
        }
    }

    private void close()
    {
        m_bClosed = true;
        stopPlayback();
        if (m_oTts != null)
        {
            try
            {
                m_oTts.shutdown();
            }
            catch (RuntimeException oIgnored)
            {
            }
            m_oTts = null;
        }
    }

    public static void setVoice(long nOwner, String strVoice, long nRevision)
    {
        withPlayer(nOwner, oPlayer ->
        {
            oPlayer.m_strRequestedVoice = strVoice;
            oPlayer.m_nVoiceRevision = nRevision;
            oPlayer.selectVoice(strVoice);
        });
    }
    public static void setRate(long nOwner, double fRate)
    {
        withPlayer(nOwner, oPlayer -> oPlayer.m_fRate = (float) Math.max(0.5, Math.min(2.0, fRate)));
    }
    public static void setPitch(long nOwner, double fPitch)
    {
        withPlayer(nOwner, oPlayer -> oPlayer.m_fPitch = (float) Math.max(0.5, Math.min(2.0, fPitch)));
    }
    public static void setVolume(long nOwner, double fVolume)
    {
        withPlayer(nOwner, oPlayer -> oPlayer.m_fVolume = (float) Math.max(0.0, Math.min(1.0, fVolume)));
    }
    public static void speak(long nOwner, String strText, long nToken)
    {
        withPlayer(nOwner, oPlayer -> oPlayer.play(strText, nToken));
    }
    public static void stop(long nOwner)
    {
        withPlayer(nOwner, SpeechPlayer::stopPlayback);
    }
    public static void destroy(long nOwner)
    {
        m_oMain.post(() ->
        {
            final SpeechPlayer oPlayer = m_oPlayers.remove(nOwner);
            if (oPlayer != null)
            {
                oPlayer.close();
            }
        });
    }
    public static boolean canOpenSettings(Activity oActivity)
    {
        if (oActivity == null)
        {
            return false;
        }
        try
        {
            return new Intent("com.android.settings.TTS_SETTINGS")
                .resolveActivity(oActivity.getPackageManager()) != null;
        }
        catch (RuntimeException oIgnored)
        {
            return false;
        }
    }
    public static boolean openSettings(Activity oActivity)
    {
        if (!canOpenSettings(oActivity))
        {
            return false;
        }
        final Intent oIntent = new Intent("com.android.settings.TTS_SETTINGS");
        oActivity.runOnUiThread(() ->
        {
            try
            {
                oActivity.startActivity(oIntent);
            }
            catch (RuntimeException oIgnored)
            {
            }
        });
        return true;
    }
}
