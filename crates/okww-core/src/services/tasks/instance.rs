use crate::services::*;
impl Host {
    fn restart_dialog(&self) -> Result<f64> {
        self.action(Op::JoinDialog, &[])?;
        self.action(Op::FindCancelLine, &[])?;
        if self.flag(Op::DialogResource, &[])?
            && (self.flag(Op::DialogConfirm, &[])? || self.flag(Op::CancelLineExists, &[])?)
        {
            self.action(Op::AdoptCancelLine, &[])?;
            return Ok(1.);
        }
        if self.flag(Op::FindRestartLine, &[])? {
            return Ok(2.);
        }
        if self.flag(Op::DialogRestart, &[])?
            && self.flag(Op::DialogConfirm, &[])?
            && self.flag(Op::FindConfirmLine, &[])?
        {
            return Ok(3.);
        }
        Ok(0.)
    }
    fn exit_context(&self) -> Result<bool> {
        self.action(Op::JoinDialog, &[])?;
        Ok(self.flag(Op::DialogExit, &[])?
            && self.flag(Op::DialogExitConfirm, &[])?
            && !self.flag(Op::DialogForbidden, &[])?)
    }
    fn exit_confirmation(&self, width: f64) -> Result {
        if !self.flag(Op::ExitContext, &[])? {
            return Ok(());
        }
        let count = self.scalar(Op::DialogLineCount, &[])? as usize;
        for index in 0..count {
            if self.flag(Op::IsExitConfirmLine, &[index as f64])?
                && self.scalar(Op::DialogCenterX, &[index as f64])? > width * 0.5
                && (!self.flag(Op::SelectedLineExists, &[])?
                    || self.scalar(Op::DialogLineX, &[index as f64])?
                        > self.scalar(Op::SelectedLineX, &[])?)
            {
                self.action(Op::StoreSelectedLine, &[index as f64])?;
            }
        }
        Ok(())
    }
    fn restart_boss(&self, verify_fast_restart: bool) -> Result {
        let mut tracker = crate::farming::RestartState::default();
        let mut request_frame = -1.;
        let mut requested_at = 0.;
        let mut clicked_kind = 0;
        let mut last_frame = -1.;
        let mut old_timer = -1.;
        let mut health_was_absent = false;
        let mut previous_evidence = (false, -1.);
        for _ in 0..120 {
            self.action(Op::Refresh, &[])?;
            if self.flag(Op::CombatIntroVisible, &[])? {
                self.action(Op::CloseCombatIntro, &[])?;
                continue;
            }
            let kind = self.scalar(Op::ReadRestartDialog, &[])? as i32;
            let hud = self.flag(Op::InTeam, &[])?;
            let realm = hud && self.flag(Op::NavInRealm, &[])?;
            let mut frame = -1.;
            let mut fast_poll = false;
            if verify_fast_restart {
                frame = self.scalar(Op::RestartFrame, &[])?;
                let waiting = tracker.requested != 0;
                let elapsed = if waiting {
                    self.scalar(Op::Now, &[])? - requested_at
                } else {
                    0.
                };
                fast_poll = waiting && elapsed < 3.;
                // A frame captured before the click, or a lingering copy of
                // the clicked dialog, cannot confirm or repeat the request.
                if (waiting && (frame == request_frame || (kind == clicked_kind && elapsed < 0.7)))
                    || (kind == 0 && self.flag(Op::RestartPendingModal, &[])?)
                {
                    previous_evidence = (false, -1.);
                    self.action(Op::Sleep, &[if fast_poll { 0.12 } else { 0.5 }])?;
                    continue;
                }
                if waiting && kind == 0 && hud && self.flag(Op::RestartRewardObjective, &[])? {
                    previous_evidence = (false, -1.);
                    if elapsed >= 2. {
                        // The request returned to the old round. Reopen the
                        // menu using the same bounded restart tracker.
                        tracker.requested = 0;
                        tracker.loading = 0;
                    }
                    self.action(Op::Sleep, &[0.35])?;
                    continue;
                }
                if kind == 0 && hud && frame != last_frame {
                    last_frame = frame;
                    if !waiting && !self.flag(Op::RestartWorldMarker, &[])? {
                        let timer = self.scalar(Op::RestartTimer, &[])?;
                        if timer >= 0. {
                            old_timer = timer;
                        }
                        health_was_absent = !self.flag(Op::EchoHasHealthBar, &[])?;
                    } else if waiting
                        && elapsed >= 0.2
                        && !self.flag(Op::RestartWorldMarker, &[])?
                    {
                        let health = self.flag(Op::EchoHasHealthBar, &[])?;
                        health_was_absent |= !health;
                        let new_health = health && health_was_absent;
                        let timer = self.scalar(Op::RestartTimer, &[])?;
                        let reset_timer = old_timer >= 0. && timer >= old_timer + 3.;
                        let (previous_health, previous_timer) = previous_evidence;
                        let confirmed_timer = reset_timer
                            && previous_timer >= 0.
                            && timer <= previous_timer + 0.25
                            && timer >= previous_timer - 3.;
                        if (new_health && previous_health) || confirmed_timer {
                            if self.flag(Op::HasInfo, &[])? {
                                self.action(
                                    Op::RestartCompleteInfo,
                                    &[if confirmed_timer { 2. } else { 1. }],
                                )?;
                            }
                            return Ok(());
                        }
                        previous_evidence = (new_health, if reset_timer { timer } else { -1. });
                    } else {
                        previous_evidence = (false, -1.);
                    }
                } else if kind != 0 || !hud {
                    previous_evidence = (false, -1.);
                }
            }
            let action = tracker.step(kind, hud, realm);
            if action == 4 {
                if self.flag(Op::HasInfo, &[])? {
                    self.action(Op::RestartCompleteInfo, &[0.])?;
                }
                return Ok(());
            }
            if action == 5 {
                break;
            }
            if action == 3 {
                if self.flag(Op::RestartButtonExists, &[])? {
                    self.action(Op::ClickRestartButton, &[if kind == 1 { 0.3 } else { 0. }])?;
                    if verify_fast_restart && kind != 1 {
                        request_frame = frame;
                        requested_at = self.scalar(Op::Now, &[])?;
                        clicked_kind = kind;
                        tracker.loading = 0;
                    }
                } else {
                    self.action(Op::Key, &[0., 0.3])?;
                }
                if verify_fast_restart && kind == 1 {
                    old_timer = -1.;
                    health_was_absent = false;
                }
                if kind != 1 {
                    continue;
                }
            } else if action == 2 {
                self.action(Op::Key, &[0., 0.5])?;
            }
            self.action(Op::Sleep, &[if fast_poll { 0.12 } else { 0.5 }])?;
        }
        self.action(
            Op::Throw,
            &[
                200.,
                tracker.requested as f64,
                tracker.loading as f64,
                old_timer,
            ],
        )?;
        Err(HOST_ERROR)
    }
    fn completed_nest(&self) -> Result<bool> {
        let count = self.scalar(Op::ReadCompleteText, &[])? as usize;
        for index in 0..count {
            if self.flag(Op::LineIsComplete, &[index as f64])? {
                return Ok(true);
            }
        }
        Ok(false)
    }
    fn outside_nest(&self) -> Result<bool> {
        Ok(self.flag(Op::NavInWorld, &[])? && !self.completed_nest()?)
    }
    fn leave_nest(&self, send_escape: bool) -> Result {
        if self.outside_nest()? {
            return Ok(());
        }
        let mut escape = !send_escape;
        for _ in 0..4 {
            self.action(Op::Refresh, &[])?;
            if self.outside_nest()? {
                return Ok(());
            }
            self.action(Op::ReadNestExitDialog, &[])?;
            self.action(Op::ReadExitConfirmation, &[])?;
            if !self.flag(Op::ExitButtonExists, &[])?
                && self.flag(Op::ReadExitContext, &[])?
                && self.flag(Op::FindNestConfirmFeature, &[0.6])?
            {
                self.action(Op::FeatureAsExitButton, &[])?;
            }
            if self.flag(Op::ExitButtonExists, &[])? {
                self.action(Op::ClickExitButton, &[1.])?;
            } else {
                let mut prompt = false;
                if self.flag(Op::FindExitPrompt, &[0.7])? {
                    let count = self.scalar(Op::ReadExitPromptText, &[])? as usize;
                    for index in 0..count {
                        prompt |= self.flag(Op::LineIsExit, &[index as f64])?;
                    }
                }
                if prompt {
                    self.action(Op::Key, &[6., 1.])?;
                } else {
                    let count = self.scalar(Op::ReadNestExitLines, &[])? as usize;
                    let mut found = false;
                    for index in 0..count {
                        if self.flag(Op::LineIsExit, &[index as f64])? {
                            self.action(Op::SelectNestExitLine, &[index as f64])?;
                            found = true;
                            break;
                        }
                    }
                    if found {
                        self.action(Op::ClickExitButton, &[1.])?;
                    } else {
                        if self.flag(Op::InTeam, &[])? && !escape {
                            self.action(Op::Key, &[0., 1.])?;
                            escape = true;
                        } else {
                            self.action(Op::Sleep, &[1.])?;
                        }
                        continue;
                    }
                }
            }
            if self.wait_for(|| self.outside_nest(), 4.)? {
                return Ok(());
            }
        }
        if !self.wait_for(|| self.outside_nest(), 45.)? {
            return self.fail(201.);
        }
        Ok(())
    }
    pub(in crate::services) fn run_instance(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        let mut value = 0.;
        match method {
            Method::CombineDialog => {
                let count = self.scalar(Op::DialogLineCount, &[])? as usize;
                for index in 0..count {
                    self.action(Op::AppendCleanLine, &[index as f64])?;
                }
            }
            Method::ExactDialog => {
                let count = self.scalar(Op::DialogLineCount, &[])? as usize;
                for index in 0..count {
                    if self.flag(Op::ExactLineMatch, &[index as f64])? {
                        self.action(Op::StoreSelectedLine, &[index as f64])?;
                        break;
                    }
                }
            }
            Method::RestartDialog => value = self.restart_dialog()?,
            Method::NestExitText => value = self.flag(Op::MatchExitText, &[])? as u8 as f64,
            Method::NestCompleteText => value = self.flag(Op::MatchCompleteText, &[])? as u8 as f64,
            Method::NestExitContext => value = self.exit_context()? as u8 as f64,
            Method::NestExitConfirmation => self.exit_confirmation(a[0])?,
            Method::RestartBoss => self.restart_boss(a[0] != 0.)?,
            Method::CompletedNest => value = self.completed_nest()? as u8 as f64,
            Method::LeaveNest => self.leave_nest(a[0] != 0.)?,
            _ => return Err(INVALID_ACTION),
        }
        Ok([value, 0., 0.])
    }
}
