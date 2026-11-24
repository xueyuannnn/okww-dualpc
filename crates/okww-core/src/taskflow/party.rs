use super::*;

fn party_from_markers(mask: u8, known_size: f64) -> [f64; 3] {
    let found = mask.count_ones();
    if found == 2 {
        let current = (0..3).find(|i| mask & (1 << i) == 0).unwrap_or(0);
        return [1., f64::from(current), 3.];
    }
    if found == 1 && (known_size == 3. || mask & 4 != 0) {
        // One visible number cannot identify the active member of a trio.
        // Keep its size, but require another observation before a rotation.
        return [1., -1., 3.];
    }
    if mask == 2 {
        return [1., 0., 2.];
    }
    if mask == 1 {
        return [1., 1., 2.];
    }
    [0., -1., 0.]
}

impl Host {
    pub(super) fn local_party(&self) -> Result<[f64; 3]> {
        let mut mask = 0;
        for index in 0..3 {
            if self.flag(Op::PartyProbe, &[f64::from(index), 0.])?
                || self.flag(Op::PartyProbe, &[f64::from(index), 1.])?
            {
                mask |= 1 << index;
            }
        }
        self.publish_party_markers(mask)
    }

    pub(super) fn remote_party(&self) -> Result<[f64; 3]> {
        let mut mask: u8 = 0;
        for index in 0..3 {
            if self.flag(Op::PartyProbe, &[f64::from(index), 0.])?
                || self.flag(Op::PartyProbe, &[f64::from(index), 1.])?
            {
                mask |= 1 << index;
            }
        }
        // Require two separately located numbers and at least one strong
        // anchor. A lone weak match must never establish a party.
        if mask.count_ones() < 2 {
            let mut strong = mask;
            let mut weak = mask;
            for index in 0..3 {
                if mask & (1 << index) == 0 {
                    let score = self.call(Op::PartyProbe, &[f64::from(index), 2.])?;
                    if score[0] != 0. {
                        strong |= 1 << index;
                    }
                    if score[1] != 0. {
                        weak |= 1 << index;
                    }
                }
            }
            if strong != 0 && weak.count_ones() == 2 {
                mask = weak;
            }
        }
        self.publish_party_markers(mask)
    }

    fn publish_party_markers(&self, mask: u8) -> Result<[f64; 3]> {
        let state = self.call(Op::RemotePartyState, &[f64::from(mask)])?;
        let result = party_from_markers(mask, state[0]);
        if result[0] != 0. {
            self.action(Op::SetLoggedIn, &[1.])?;
        }
        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn every_trio_position_is_identified() {
        assert_eq!(party_from_markers(6, 0.), [1., 0., 3.]);
        assert_eq!(party_from_markers(5, 0.), [1., 1., 3.]);
        assert_eq!(party_from_markers(3, 0.), [1., 2., 3.]);
    }
    #[test]
    fn single_missing_marker_never_shrinks_a_known_trio() {
        for mask in [1, 2, 4] {
            assert_eq!(party_from_markers(mask, 3.), [1., -1., 3.]);
        }
        assert_eq!(party_from_markers(4, 0.), [1., -1., 3.]);
        assert_eq!(party_from_markers(0, 3.), [0., -1., 0.]);
        assert_eq!(party_from_markers(7, 3.), [0., -1., 0.]);
    }
    #[test]
    fn two_member_parties_remain_supported() {
        assert_eq!(party_from_markers(1, 0.), [1., 1., 2.]);
        assert_eq!(party_from_markers(2, 2.), [1., 0., 2.]);
    }
}
