#include "TransceiverBase.hpp"

#include <exception>

#include <QDateTime>
#include <QString>
#include <QTimer>
#include <QThread>
#include <QDebug>

#include "moc_TransceiverBase.cpp"

namespace
{
  auto const unexpected = TransceiverBase::tr ("Unexpected rig error");
}

void TransceiverBase::start (unsigned sequence_number) noexcept
{
  QString message;
  try
    {
      last_sequence_number_ = sequence_number;
      may_update u {this, true};
      shutdown ();
      startup ();
    }
  catch (std::exception const& e)
    {
      message = e.what ();
    }
  catch (...)
    {
      message = unexpected;
    }
  if (!message.isEmpty ())
    {
      offline (message);
    }
}

void TransceiverBase::set (TransceiverState const& s,
                           unsigned sequence_number) noexcept
{
  TRACE_CAT ("TransceiverBase", "#:" << sequence_number << s);

  QString message;
  try
    {
      last_sequence_number_ = sequence_number;
      may_update u {this, true};
//      printf("%s Transiever set\n",QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str());
      bool was_online {requested_.online ()};
      auto ms = set_freq_time;
      if (!s.online () && was_online)
        {
//          printf("%s shutdown",QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str());
          shutdown ();
        }
      else if (s.online () && !was_online)
        {
//          printf("%s shutdown",QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str());
          shutdown ();
//          printf("%s start",QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str());
          startup ();
        }
      if (requested_.online ())
        {
          bool ptt_on {false};
          bool ptt_off {false};
          if (requested_.fast_mode() != s.fast_mode()) {
//            printf("%s Timing fast_mode=%d\n",QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),s.fast_mode());
            do_post_fast_mode (s.fast_mode());
            requested_.fast_mode (s.fast_mode ());
          }
          if (s.ptt () != requested_.ptt ())
            {
              ptt_on = s.ptt ();
              ptt_off = !s.ptt ();
            }
          if (ptt_off)
            {
//              printf("%s Timing ptt_off\n",QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str());
              do_ptt (false);
              do_post_ptt (false);
              QThread::msleep (100); // some rigs cannot process CAT
                                     // commands while switching from
                                     // Tx to Rx
            }
          if (s.frequency ()    // ignore bogus zero frequencies
              && ((s.frequency () != requested_.frequency () // and QSY
                   || (s.mode () != UNK && s.mode () != requested_.mode ())) // or mode change
                  || ptt_off))       // or just returned to rx
            {
              auto ms2 = QDateTime::currentMSecsSinceEpoch();
//              printf("%s Timing do_frequency %lld\n",QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),s.frequency ());
              ms = 0;
              do_frequency (s.frequency (), s.mode (), ptt_off);
              do_post_frequency (s.frequency (), s.mode ());

              // record what actually changed
              requested_.frequency (actual_.frequency ());
              requested_.mode (actual_.mode ());
              set_freq_time = QDateTime::currentMSecsSinceEpoch() - ms2;
            }
          if (!s.tx_frequency ()
              || (s.tx_frequency () > 10000 // ignore bogus startup values
                  && s.tx_frequency () < std::numeric_limits<Frequency>::max () - 10000))
            {
              if ((s.tx_frequency () != requested_.tx_frequency () // and QSY
                   || (s.mode () != UNK && s.mode () != requested_.mode ())) // or mode change
                  // || s.split () != requested_.split ())) // or split change
                  || (s.tx_frequency () && ptt_on)) // or about to tx split
                {
//                  printf("%s Timing do_tx_frequency %lld\n",QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),s.frequency ());
                  ms = 0;
                  do_tx_frequency (s.tx_frequency (), s.mode (), ptt_on);
                  do_post_tx_frequency (s.tx_frequency (), s.mode ());

                  // record what actually changed
                  requested_.tx_frequency (actual_.tx_frequency ());
                  requested_.split (actual_.split ());
                }
            }
          if (ptt_on)
            {
//              printf("%s Timing ptt_on\n",QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str());
              do_ptt (true);
              do_post_ptt (true);
              QThread::msleep (100 + ms); // some rigs cannot process CAT
                                     // commands while switching from
                                     // Rx to Tx
            }

          // record what actually changed
          requested_.ptt (actual_.ptt ());
        } //else printf ("%s NOT ONLINE\n",QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str());
//      printf("%s Transiever set end\n",QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str());
    }
  catch (std::exception const& e)
    {
      message = e.what ();
    }
  catch (...)
    {
      message = unexpected;
    }
  
  if (!message.isEmpty ())
    {
//      printf("%s Transiever set message %s\n",QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz").toStdString().c_str(),message.toStdString().c_str());
      offline (message);
    }
}

void TransceiverBase::startup ()
{
  QString message;
  try
    {
      actual_.online (true);
      requested_.online (true);
      auto res = do_start ();
      do_post_start ();
      Q_EMIT resolution (res);
    }
  catch (std::exception const& e)
    {
      message = e.what ();
    }
  catch (...)
    {
      message = unexpected;
    }
  if (!message.isEmpty ())
    {
      offline (message);
    }
}

void TransceiverBase::shutdown ()
{
  may_update u {this};
  if (requested_.online ())
    {
      try
        {
          // try and ensure PTT isn't left set
          do_ptt (false);
          do_post_ptt (false);
          if (requested_.split ())
            {
              // try and reset split mode
              do_tx_frequency (0, UNK, true);
              do_post_tx_frequency (0, UNK);
            }
        }
      catch (...)
        {
          // don't care about exceptions
        }
    }
  do_stop ();
  do_post_stop ();
  actual_ = TransceiverState {};
  requested_ = TransceiverState {};
}

void TransceiverBase::stop () noexcept
{
  QString message;
  try
    {
      shutdown ();
    }
  catch (std::exception const& e)
    {
      message = e.what ();
    }
  catch (...)
    {
      message = unexpected;
    }
  if (!message.isEmpty ())
    {
      offline (message);
    }
  else
    {
      Q_EMIT finished ();
    }
}

void TransceiverBase::update_rx_frequency (Frequency rx)
{
  if (rx)
    {
      actual_.frequency (rx);
      requested_.frequency (rx);    // track rig changes
    }
}

void TransceiverBase::update_other_frequency (Frequency tx)
{
  actual_.tx_frequency (tx);
}

void TransceiverBase::update_split (bool state)
{
  actual_.split (state);
}

void TransceiverBase::update_mode (MODE m)
{
  actual_.mode (m);
  requested_.mode (m);    // track rig changes
}

void TransceiverBase::update_PTT (bool state)
{
  actual_.ptt (state);
}

void TransceiverBase::update_level (int l)
{
  actual_.level (l);
//  requested_.level (l);    // track rig changes
}

void TransceiverBase::update_power (unsigned int p)
{
  actual_.power (p);
//  requested_.power (p);    // track rig changes
}

void TransceiverBase::update_complete (bool force_signal)
{
  if ((do_pre_update () && actual_ != last_) || force_signal)
    {
      Q_EMIT update (actual_, last_sequence_number_);
      last_ = actual_;
    }
}

void TransceiverBase::offline (QString const& reason)
{
  Q_EMIT failure (reason);
  try
    {
      shutdown ();
    }
  catch (...)
    {
      // don't care
    }
}
